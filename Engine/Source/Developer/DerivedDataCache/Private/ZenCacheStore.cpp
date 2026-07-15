// Copyright Epic Games, Inc. All Rights Reserved.

#include "Algo/AnyOf.h"
#include "Algo/Compare.h"
#include "Async/ManualResetEvent.h"
#include "Async/UniqueLock.h"
#include "BatchView.h"
#include "Containers/AnsiString.h"
#include "DerivedDataBackendInterface.h"
#include "DerivedDataCachePrivate.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataCacheUsageStats.h"
#include "DerivedDataChunk.h"
#include "DerivedDataHttpRequestQueue.h"
#include "DerivedDataLegacyCacheStore.h"
#include "DerivedDataRequest.h"
#include "DerivedDataRequestOwner.h"
#include "DerivedDataSerialization.h"
#include "Experimental/ZenServerInterface.h"
#include "Experimental/ZenStatistics.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/Thread.h"
#include "Http/HttpClient.h"
#include "Http/HttpHostBuilder.h"
#include "Logging/StructuredLog.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Optional.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryPackage.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Serialization/LargeMemoryWriter.h"
#include "String/LexFromString.h"
#include "Templates/Function.h"
#include "ZenAsyncCbPackageReceiver.h"
#include "ZenBackendUtils.h"
#include "ZenCbPackageReceiver.h"
#include "ZenSerialization.h"

TRACE_DECLARE_ATOMIC_INT_COUNTER(ZenDDC_Get, TEXT("ZenDDC Get"));
TRACE_DECLARE_ATOMIC_INT_COUNTER(ZenDDC_GetHit, TEXT("ZenDDC Get Hit"));
TRACE_DECLARE_ATOMIC_INT_COUNTER(ZenDDC_Put, TEXT("ZenDDC Put"));
TRACE_DECLARE_ATOMIC_INT_COUNTER(ZenDDC_PutHit, TEXT("ZenDDC Put Hit"));
TRACE_DECLARE_ATOMIC_INT_COUNTER(ZenDDC_BytesReceived, TEXT("ZenDDC Bytes Received"));
TRACE_DECLARE_ATOMIC_INT_COUNTER(ZenDDC_BytesSent, TEXT("ZenDDC Bytes Sent"));
TRACE_DECLARE_ATOMIC_INT_COUNTER(ZenDDC_CacheRecordRequestCountInFlight, TEXT("ZenDDC CacheRecord Request Count"));
TRACE_DECLARE_ATOMIC_INT_COUNTER(ZenDDC_ChunkRequestCountInFlight, TEXT("ZenDDC Chunk Request Count"));

static TAutoConsoleVariable<bool> CVarZenCacheLimitSandboxProcessLifetime(
	TEXT("zen.cache.limitsandboxprocesslifetime"),
	true,
	TEXT("When true, Zen server process for sandbox cache instances will be shut down when the process which started it exits. When false, process will persist.\n")
	TEXT("Default: true"),
	ECVF_ReadOnly);

namespace UE::DerivedData
{
// Defined in HttpCacheStore.cpp
FString ParseConfigParamWithOverrides(const TCHAR* NodeName, const TCHAR* Config, const TCHAR* ParamName, FStringView ExistingValue, bool bIsSecret);

template<typename T>
void ForEachBatch(const int32 BatchSize, const int32 TotalCount, T&& Fn)
{
	check(BatchSize > 0);

	if (TotalCount > 0)
	{
		const int32 BatchCount = FMath::DivideAndRoundUp(TotalCount, BatchSize);
		const int32 Last = TotalCount - 1;

		for (int32 BatchIndex = 0; BatchIndex < BatchCount; BatchIndex++)
		{
			const int32 BatchFirstIndex	= BatchIndex * BatchSize;
			const int32 BatchLastIndex	= FMath::Min(BatchFirstIndex + BatchSize - 1, Last);

			Fn(BatchFirstIndex, BatchLastIndex);
		}
	}
}

struct FZenCacheStoreParams
{
	FString Name;
	FString Host;
	FString Namespace;
	FString Sandbox;
	int32 MaxBatchPutKB = 1024;
	int32 RecordBatchSize = 8;
	int32 ChunksBatchSize = 8;
	float ReadyWaitMs = 5000.0f;
	float ReadyWaitBuildMachineMs = 120000.0f;
	float DeactivateAtMs = -1.0f;
	TOptional<bool> bLocal;
	TOptional<bool> bRemote;
	bool bFlush = false;
	bool bReadOnly = false;
	bool bWriteOnly = false;
	bool bBypassProxy = true;
	bool bNamespacedRpcEndpoint = false;
	bool bReadyWaitRetry = false;
	bool bReadyWaitRetryBuildMachine = true;

	void Parse(const TCHAR* NodeName, const TCHAR* Config);
};

class FZenPerformanceEvaluationTracker
{
public:
	void AddLatency(FMonotonicTimeSpan Latency)
	{
		if (Latency.IsInfinity() || Latency.IsZero())
		{
			return;
		}
		TUniqueLock Lock(Mutex);
		++DataPointCount;
		MinimumLatency = FMath::Min(MinimumLatency, Latency);
	}

	void MeasureAndReset(uint64& OutDataPointCount, FMonotonicTimeSpan& OutMinimumLatency)
	{
		TUniqueLock Lock(Mutex);
		OutDataPointCount = DataPointCount;
		OutMinimumLatency = MinimumLatency;
		DataPointCount = 0;
		MinimumLatency = FMonotonicTimeSpan::Infinity();
	}
private:
	FMutex Mutex;
	uint64 DataPointCount = 0;
	FMonotonicTimeSpan MinimumLatency = FMonotonicTimeSpan::Infinity();
};

/**
 * Backend for a HTTP based caching service (Zen)
 */
class FZenCacheStore final : public ILegacyCacheStore
{
public:
	
	/**
	 * Creates the cache store client, checks health status and attempts to acquire an access token.
	 *
	 * @param ServiceUrl	Base url to the service including scheme.
	 * @param Namespace		Namespace to use.
	 */
	FZenCacheStore(
		const FZenCacheStoreParams& InParams,
		ICacheStoreOwner& Owner);

	FZenCacheStore(
		UE::Zen::FServiceSettings&& Settings,
		const FZenCacheStoreParams& InParams,
		ICacheStoreOwner& Owner);

	~FZenCacheStore() final;

	inline const FSharedString& GetName() const { return NodeName; }

	/**
	 * Checks if cache service is usable (reachable and accessible).
	 * @return true if usable
	 */
	inline bool IsUsable() const { return bIsUsable; }

	/**
	 * Checks if cache service is on the local machine.
	 * @return true if it is local
	 */
	inline bool IsLocalConnection() const { return bIsLocalConnection; }

	// ICacheStore

	void Put(
		TConstArrayView<FCachePutRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutComplete&& OnComplete = FOnCachePutComplete()) final;

	void Get(
		TConstArrayView<FCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetComplete&& OnComplete) final;

	void PutValue(
		TConstArrayView<FCachePutValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutValueComplete&& OnComplete = FOnCachePutValueComplete()) final;

	void GetValue(
		TConstArrayView<FCacheGetValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetValueComplete&& OnComplete = FOnCacheGetValueComplete()) final;

	void GetChunks(
		TConstArrayView<FCacheGetChunkRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetChunkComplete&& OnComplete) final;

	// ILegacyCacheStore

	void LegacyStats(FDerivedDataCacheStatsNode& OutNode) final;

	const Zen::FZenServiceInstance& GetServiceInstance() const { return ZenService.GetInstance(); }

	class FPutOp;
	class FGetOp;
	class FPutValueOp;
	class FGetValueOp;
	class FGetChunksOp;

	template <typename T, typename... ArgTypes>
	static TRefCountPtr<T> MakeAsyncOp(ArgTypes&&... Args)
	{
		// TODO: This should in-place construct from a pre-allocated memory pool
		return TRefCountPtr<T>(new T(Forward<ArgTypes>(Args)...));
	}

private:
	bool ReadyWait(const FZenCacheStoreParams& Params);
	void Initialize(const FZenCacheStoreParams& Params);

	bool IsServiceReady();

	static FCompositeBuffer SaveRpcPackage(const FCbPackage& Package);
	void CreateRpcRequest(IRequestOwner& Owner, FHttpRequestQueue::FOnRequest&& OnRequest);
	using FOnRpcComplete = TUniqueFunction<void(const THttpUniquePtr<IHttpResponse>& HttpResponse, const FCbPackage& Response, FStringView ResponseText)>;
	void EnqueueAsyncRpc(IRequestOwner& Owner, FCbObject RequestObject, FOnRpcComplete&& OnComplete);
	void EnqueueAsyncRpc(IRequestOwner& Owner, const FCbPackage& RequestPackage, FOnRpcComplete&& OnComplete);

	void ActivatePerformanceEvaluationThread();
	void ConditionalEvaluatePerformance();
	void ConditionalUpdateStorageSize();
	void UpdateStatus();

private:
	template <typename RequestType>
	struct TRequestWithStats;

	template <typename OutContainerType, typename InContainerType, typename BucketAccessorType, typename RequestTypeAccessorType>
	static void StartRequests(
		OutContainerType& Out,
		const InContainerType& In,
		BucketAccessorType BucketAccessor,
		RequestTypeAccessorType TypeAccessor,
		const ERequestOp Op);

	[[nodiscard]] bool ValidateAndPrepareReceivedValue(
		FStringView Name,
		const FCacheKey& Key,
		const FValueId& Id,
		const FValue& Value,
		ECachePolicy Policy,
		FValue& OutValue,
		FRequestStats& Stats) const;

	void SetRequestUri(IHttpRequest& Request, FAnsiStringView Path);

	enum class EHealth
	{
		Unknown,
		Ok,
		Error,
	};

	using FOnHealthComplete = TUniqueFunction<void(const THttpUniquePtr<IHttpResponse>& HttpResponse, EHealth Health)>;
	class FHealthReceiver;
	class FAsyncHealthReceiver;

	class FRequestAsyncCbPackageReceiver;

	FSharedString NodeName;
	FString Namespace;
	UE::Zen::FScopeZenService ZenService;
	ICacheStoreOwner& StoreOwner;
	ICacheStoreStats* StoreStats = nullptr;
	THttpUniquePtr<IHttpConnectionPool> ConnectionPool;
	FHttpRequestQueue RequestQueue;
	bool bIsUsable = false;
	bool bIsLocalConnection = false;
	bool bTryEvaluatePerformance = false;
	std::atomic<bool> bDeactivatedForPerformance = false;
	int32 MaxBatchPutKB = 1024;
	int32 CacheRecordBatchSize = 8;
	int32 CacheChunksBatchSize = 8;
	FAnsiString RpcUri;
	std::atomic<int64> LastPerformanceEvaluationTicks;
	TOptional<FThread> PerformanceEvaluationThread;
	FManualResetEvent PerformanceEvaluationThreadShutdownEvent;
	std::atomic<int64> LastStorageSizeUpdateTicks;
	float DeactivateAtMs = -1.0f;
	ECacheStoreFlags OperationalFlags;
	FRequestOwner PerformanceEvaluationRequestOwner;
	FZenPerformanceEvaluationTracker PerformanceEvaluationTracker;
};

template <typename RequestType>
struct FZenCacheStore::TRequestWithStats
{
	RequestType Request;
	mutable FRequestStats Stats;

	explicit TRequestWithStats(const RequestType& InRequest)
		: Request(InRequest)
	{
	}

	void EndRequest(FZenCacheStore& Outer, const EStatus Status) const
	{
		{
			TUniqueLock Lock(Stats.Mutex.Get());
			Stats.EndTime = FMonotonicTimePoint::Now();
			Stats.Status = Status;
		}
		if (Outer.StoreStats)
		{
			Outer.StoreStats->AddRequest(Stats);
		}
		Outer.PerformanceEvaluationTracker.AddLatency(Stats.Latency);
	}
};

template <typename OutContainerType, typename InContainerType, typename BucketAccessorType, typename RequestTypeAccessorType>
void FZenCacheStore::StartRequests(
	OutContainerType& Out,
	const InContainerType& In,
	BucketAccessorType BucketAccessor,
	RequestTypeAccessorType TypeAccessor,
	const ERequestOp Op)
{
	const FMonotonicTimePoint Now = FMonotonicTimePoint::Now();
	Out.Reserve(In.Num());
	for (const auto& Request : In)
	{
		auto& RequestWithStats = Out[Out.Emplace(Request)];
		RequestWithStats.Stats.Name = Request.Name;
		RequestWithStats.Stats.Bucket = BucketAccessor(Request);
		RequestWithStats.Stats.Type = TypeAccessor(Request);
		RequestWithStats.Stats.Op = Op;
		RequestWithStats.Stats.StartTime = Now;
	}
}

void FZenCacheStore::SetRequestUri(IHttpRequest& Request, FAnsiStringView Path)
{
	using namespace Zen;

	check(Path.Len() && Path[0] == '/');

	const FZenServiceEndpoint& Endpoint = ZenService.GetInstance().GetEndpoint();

	TAnsiStringBuilder<128> Uri;
	Uri << Endpoint.GetURL();
	Uri << Path;
	Request.SetUri(Uri);

	if (Endpoint.GetSocketType() != FZenServiceEndpoint::ESocketType::Unix)
	{
		return;
	}

	FStringView UnixSocketPath = Endpoint.GetName();
	const auto& AsAnsi = StringCast<ANSICHAR>(UnixSocketPath.GetData(), UnixSocketPath.Len());
	Request.SetUnixSocketPath(AsAnsi);
}

bool FZenCacheStore::ValidateAndPrepareReceivedValue(
		FStringView Name,
		const FCacheKey& Key,
		const FValueId& Id,
		const FValue& Value,
		ECachePolicy Policy,
		FValue& OutValue,
		FRequestStats& Stats) const
{
	if (!EnumHasAnyFlags(Policy, ECachePolicy::Query))
	{
		// Ensure any data accompanying the value is removed.
		OutValue = Value.RemoveData();
		return true;
	}

	if (EnumHasAnyFlags(Policy, ECachePolicy::SkipData))
	{
		// Ensure any data accompanying the value is removed.
		OutValue = Value.RemoveData();
		return true;
	}

	if (!Value.HasData())
	{
		UE_LOGF(LogDerivedDataCache, Verbose,
			"%ls: Cache miss with missing value %ls with hash %ls for %ls from '%.*ls'",
			*GetName(), *WriteToString<16>(Id), *WriteToString<48>(Value.GetRawHash()), *WriteToString<96>(Key),
			Name.Len(), Name.GetData());
		return false;
	}

	if (Value.GetRawHash().IsZero() || Value.GetRawSize() == MAX_uint64)
	{
		UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache miss with invalid value for %ls from '%.*ls'",
			*GetName(), *WriteToString<96>(Key), Name.Len(), Name.GetData());
		OutValue = Value.RemoveData();
		return false;
	}

	if (Value.GetData().GetRawHash() != Value.GetRawHash())
	{
		UE_LOGF(LogDerivedDataCache, Display,
			"%ls: Cache miss with corrupted value %ls with hash %ls for %ls from '%.*ls'",
			*GetName(), *WriteToString<16>(Id), *WriteToString<48>(Value.GetRawHash()),
			*WriteToString<96>(Key), Name.Len(), Name.GetData());
		OutValue = Value.RemoveData();
		return false;
	}

	OutValue = Value;
	Stats.AddLogicalRead(Value);
	Stats.PhysicalReadSize += Value.GetData().GetCompressedSize();
	return true;
}

class FZenCacheStore::FPutOp final : public FRefCountedObject
{
public:
	FPutOp(FZenCacheStore& InCacheStore,
		IRequestOwner& InOwner,
		const TConstArrayView<FCachePutRequest> InRequests,
		FOnCachePutComplete&& InOnComplete)
		: CacheStore(InCacheStore)
		, Owner(InOwner)
		, OnComplete(MoveTemp(InOnComplete))
	{
		StartRequests(Requests, InRequests, [](const FCachePutRequest& Request) { return Request.Record.GetKey().Bucket; },
			[](auto&) { return ERequestType::Record; }, ERequestOp::Put);
		Batches = TBatchView<const TRequestWithStats<FCachePutRequest>>(Requests,
			[this](const TRequestWithStats<FCachePutRequest>& NextRequest) { return BatchGroupingFilter(NextRequest.Request); });
		TRACE_COUNTER_ADD(ZenDDC_Put, int64(Requests.Num()));
	}

	virtual ~FPutOp()
	{
		CacheStore.ConditionalEvaluatePerformance();
		CacheStore.ConditionalUpdateStorageSize();
	}

	void IssueRequests()
	{
		FRequestBarrier Barrier(Owner);
		for (TArrayView<const TRequestWithStats<FCachePutRequest>> Batch : Batches)
		{
			FCbPackage BatchPackage;
			FCbWriter BatchWriter;
			BatchWriter.BeginObject();
			{
				BatchWriter << ANSITEXTVIEW("Method") << "PutCacheRecords";
				BatchWriter.AddInteger(ANSITEXTVIEW("Accept"), Zen::Http::kCbPkgMagic);

				BatchWriter.BeginObject(ANSITEXTVIEW("Params"));
				{
					ECachePolicy BatchDefaultPolicy = Batch[0].Request.Policy.GetRecordPolicy();
					BatchWriter << ANSITEXTVIEW("DefaultPolicy") << *WriteToString<128>(BatchDefaultPolicy);
					BatchWriter.AddString(ANSITEXTVIEW("Namespace"), CacheStore.Namespace);

					BatchWriter.BeginArray(ANSITEXTVIEW("Requests"));
					for (const TRequestWithStats<FCachePutRequest>& RequestWithStats : Batch)
					{
						FRequestTimer RequestTimer(RequestWithStats.Stats);
						const FCachePutRequest& Request = RequestWithStats.Request;
						const FCacheRecord& Record = Request.Record;

						BatchWriter.BeginObject();
						{
							BatchWriter.SetName(ANSITEXTVIEW("Record"));
							Record.Save(BatchPackage, BatchWriter);
							if ((!Request.Policy.IsUniform()) || (Request.Policy.GetRecordPolicy() != BatchDefaultPolicy))
							{
								BatchWriter << ANSITEXTVIEW("Policy") << Request.Policy;
							}
						}
						BatchWriter.EndObject();
					}
					BatchWriter.EndArray();
				}
				BatchWriter.EndObject();
			}
			BatchWriter.EndObject();
			BatchPackage.SetObject(BatchWriter.Save().AsObject());

			auto OnRpcComplete = [this, OpRef = TRefCountPtr<FPutOp>(this), Batch](const THttpUniquePtr<IHttpResponse>& HttpResponse, const FCbPackage& Response, FStringView ResponseText)
			{
				const bool bCanceled = !HttpResponse || HttpResponse->GetErrorCode() == EHttpErrorCode::Canceled;

				int32 RequestIndex = 0;
				if (!bCanceled && HttpResponse->GetErrorCode() == EHttpErrorCode::None && HttpResponse->GetStatusCode() >= 200 && HttpResponse->GetStatusCode() <= 299)
				{
					const FCbObject& ResponseObj = Response.GetObject();
					FCbFieldIterator DetailsFieldIt = ResponseObj[ANSITEXTVIEW("Details")].AsArray().CreateIterator();
					for (FCbField ResponseField : ResponseObj[ANSITEXTVIEW("Result")])
					{
						if (RequestIndex >= Batch.Num())
						{
							++DetailsFieldIt;
							++RequestIndex;
							continue;
						}

						const TRequestWithStats<FCachePutRequest>& RequestWithStats = Batch[RequestIndex++];

						// Latency can't be measured for Put operations because it is intertwined with upload time.
						RequestWithStats.Stats.Latency = FMonotonicTimeSpan::Infinity();

						const FCacheKey& Key = RequestWithStats.Request.Record.GetKey();
						const bool bPutSucceeded = ResponseField.AsBool();

						bPutSucceeded ? OnHit(RequestWithStats, DetailsFieldIt.AsObject()) : OnMiss(RequestWithStats, DetailsFieldIt.AsObject());
						++DetailsFieldIt;
					}
					if (RequestIndex != Batch.Num())
					{
						UE_LOGF(LogDerivedDataCache, Display,
							"%ls: Invalid response received from PutCacheRecords RPC: %d results expected, received %d, from %ls",
							*CacheStore.GetName(), Batch.Num(), RequestIndex, *WriteToString<256>(*HttpResponse));
					}
				}
				else if (!bCanceled && (HttpResponse->GetStatusCode() != 404))
				{
					UE_LOGF(LogDerivedDataCache, Display,
						"%ls: Error response received from PutCacheRecords RPC: from %ls. \"%.*ls\"",
						*CacheStore.GetName(), *WriteToString<256>(*HttpResponse), ResponseText.Len(), ResponseText.GetData());
				}

				for (const TRequestWithStats<FCachePutRequest>& RequestWithStats : Batch.RightChop(RequestIndex))
				{
					if (bCanceled)
					{
						OnCanceled(RequestWithStats);
					}
					else
					{
						OnMiss(RequestWithStats, FCbObject());
					}
				}
			};
			CacheStore.EnqueueAsyncRpc(Owner, BatchPackage, MoveTemp(OnRpcComplete));
		}
	}

private:
	EBatchView BatchGroupingFilter(const FCachePutRequest& NextRequest)
	{
		const FCacheRecord& Record = NextRequest.Record;
		uint64 RecordSize = sizeof(FCacheKey) + Record.GetMeta().GetSize();
		for (const FValueWithId& Value : Record.GetValues())
		{
			RecordSize += Value.GetData().GetCompressedSize();
		}
		BatchSize += RecordSize;
		if (BatchSize > CacheStore.MaxBatchPutKB*1024)
		{
			BatchSize = RecordSize;
			return EBatchView::NewBatch;
		}
		return EBatchView::Continue;
	}

	void OnHit(const TRequestWithStats<FCachePutRequest>& RequestWithStats, FCbObject&& DetailsObject)
	{
		FRequestTimer RequestTimer(RequestWithStats.Stats);

		if (const FCbObject& Meta = RequestWithStats.Request.Record.GetMeta())
		{
			RequestWithStats.Stats.PhysicalWriteSize += Meta.GetSize();
		}
		for (const FValueWithId& Value : RequestWithStats.Request.Record.GetValues())
		{
			RequestWithStats.Stats.AddLogicalWrite(Value);
			RequestWithStats.Stats.PhysicalWriteSize += Value.GetData().GetCompressedSize();
		}

		TRACE_COUNTER_ADD(ZenDDC_BytesSent, int64(RequestWithStats.Stats.PhysicalWriteSize));
		TRACE_COUNTER_INCREMENT(ZenDDC_PutHit);

		FCachePutResponse Response = RequestWithStats.Request.MakeResponse(EStatus::Ok);

		if (DetailsObject)
		{
			if (FCbObject ExistingRecordObject = DetailsObject[ANSITEXTVIEW("Record")].AsObject())
			{
				FCbPackage ExistingRecordPackage(ExistingRecordObject);
				FOptionalCacheRecord ExistingRecord = FCacheRecord::Load(ExistingRecordPackage);

				// Report puts of non-deterministic records by ignoring known-non-deterministic values.
				const auto MakeValueTuple = [&RequestWithStats](const FValueWithId& Value) -> TTuple<FValueId, FIoHash>
				{
					return !EnumHasAnyFlags(RequestWithStats.Request.Policy.GetValuePolicy(Value.GetId()), ECachePolicy::NonDeterministic)
						? MakeTuple(Value.GetId(), Value.GetRawHash())
						: TTuple<FValueId, FIoHash>{};
				};
				if (ExistingRecord && !Algo::CompareBy(ExistingRecord.Get().GetValues(), RequestWithStats.Request.Record.GetValues(), MakeValueTuple))
				{
					UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache put found non-deterministic record for %ls from '%ls'",
						*CacheStore.GetName(), *WriteToString<96>(RequestWithStats.Request.Record.GetKey()), *RequestWithStats.Request.Name);
				}

				// Compare the whole record to check whether the record from the cache needs to be returned.
				if (ExistingRecord && !Algo::Compare(ExistingRecord.Get().GetValues(), RequestWithStats.Request.Record.GetValues()))
				{
					Response.Record = ExistingRecord.Get();
				}

				// Compare the metadata to check whether the metadata from the cache needs to be returned.
				if (ExistingRecord && !ExistingRecord.Get().GetMeta().Equals(Response.Record.GetMeta()))
				{
					Response.Record = FCacheRecord::CreateByMove(Response.Record.GetKey(), CopyTemp(ExistingRecord.Get().GetMeta()),
						TArray<FValueWithId>(Response.Record.GetValues()));
				}
			}
		}

		// A put may request data for values that were not in the incoming request.
		// Check for that case and fetch the record from the cache if that is the case.
		if (!EnumHasAnyFlags(RequestWithStats.Request.Policy.GetRecordPolicy(), ECachePolicy::SkipData) &&
			Algo::AnyOf(Response.Record.GetValues(), [&RequestWithStats](const FValueWithId& Value)
			{
				return !Value.HasData() && !EnumHasAnyFlags(RequestWithStats.Request.Policy.GetValuePolicy(Value.GetId()), ECachePolicy::SkipData);
			}))
		{
			RequestTimer.Stop();
			ForwardAsGet(RequestWithStats);
			return;
		}

		UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache put complete for %ls from '%ls'",
			*CacheStore.GetName(), *WriteToString<96>(RequestWithStats.Request.Record.GetKey()), *RequestWithStats.Request.Name);

		RequestTimer.Stop();
		RequestWithStats.EndRequest(CacheStore, EStatus::Ok);
		OnComplete(MoveTemp(Response));
	}

	void OnMiss(const TRequestWithStats<FCachePutRequest>& RequestWithStats, FCbObject&& DetailsObject)
	{
		FRequestTimer RequestTimer(RequestWithStats.Stats);
		FUtf8StringView DetailsMessage = DetailsObject[ANSITEXTVIEW("Message")].AsString();
		if (DetailsObject && !DetailsMessage.IsEmpty())
		{
			UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache put failed with message '%ls' for '%ls' from '%ls'",
				*CacheStore.GetName(), *WriteToString<64>(DetailsMessage), *WriteToString<96>(RequestWithStats.Request.Record.GetKey()), *RequestWithStats.Request.Name);
		}
		else
		{
			UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache put failed for '%ls' from '%ls'",
				*CacheStore.GetName(), *WriteToString<96>(RequestWithStats.Request.Record.GetKey()), *RequestWithStats.Request.Name);
		}

		RequestTimer.Stop();
		RequestWithStats.EndRequest(CacheStore, EStatus::Error);
		OnComplete(RequestWithStats.Request.MakeResponse(EStatus::Error));
	}

	void OnCanceled(const TRequestWithStats<FCachePutRequest>& RequestWithStats)
	{
		FRequestTimer RequestTimer(RequestWithStats.Stats);
		UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache put failed with canceled request for '%ls' from '%ls'",
			*CacheStore.GetName(), *WriteToString<96>(RequestWithStats.Request.Record.GetKey()), *RequestWithStats.Request.Name);

		RequestTimer.Stop();
		RequestWithStats.EndRequest(CacheStore, EStatus::Canceled);
		OnComplete(RequestWithStats.Request.MakeResponse(EStatus::Canceled));
	}

	void ForwardAsGet(const TRequestWithStats<FCachePutRequest>& RequestWithStats);

	FZenCacheStore& CacheStore;
	IRequestOwner& Owner;
	TArray<TRequestWithStats<FCachePutRequest>, TInlineAllocator<1>> Requests;
	uint64 BatchSize = 0;
	TBatchView<const TRequestWithStats<FCachePutRequest>> Batches;
	FOnCachePutComplete OnComplete;
};

class FZenCacheStore::FGetOp final : public FRefCountedObject
{
public:
	FGetOp(FZenCacheStore& InCacheStore,
		IRequestOwner& InOwner,
		const TConstArrayView<FCacheGetRequest> InRequests,
		FOnCacheGetComplete&& InOnComplete)
		: CacheStore(InCacheStore)
		, Owner(InOwner)
		, OnComplete(MoveTemp(InOnComplete))
	{
		StartRequests(Requests, InRequests, [](const FCacheGetRequest& Request) { return Request.Key.Bucket; },
			[](auto&) { return ERequestType::Record; }, ERequestOp::Get);
		TRACE_COUNTER_ADD(ZenDDC_Get, int64(Requests.Num()));
		TRACE_COUNTER_ADD(ZenDDC_CacheRecordRequestCountInFlight, int64(Requests.Num()));
	}

	virtual ~FGetOp()
	{
		TRACE_COUNTER_SUBTRACT(ZenDDC_CacheRecordRequestCountInFlight, int64(Requests.Num()));

		CacheStore.ConditionalEvaluatePerformance();
		CacheStore.ConditionalUpdateStorageSize();
	}

	void IssueRequests()
	{
		FRequestBarrier Barrier(Owner);
		ForEachBatch(CacheStore.CacheRecordBatchSize, Requests.Num(),
			[this](int32 BatchFirst, int32 BatchLast)
		{
			TConstArrayView<TRequestWithStats<FCacheGetRequest>> Batch(Requests.GetData() + BatchFirst, BatchLast - BatchFirst + 1);

			FCbWriter BatchRequest;
			BatchRequest.BeginObject();
			{
				BatchRequest << ANSITEXTVIEW("Method") << ANSITEXTVIEW("GetCacheRecords");
				BatchRequest.AddInteger(ANSITEXTVIEW("Accept"), Zen::Http::kCbPkgMagic);
				if (CacheStore.bIsLocalConnection)
				{
					BatchRequest.AddInteger(ANSITEXTVIEW("AcceptFlags"), static_cast<uint32_t>(Zen::Http::RpcAcceptOptions::kAllowLocalReferences));
					BatchRequest.AddInteger(ANSITEXTVIEW("Pid"), FPlatformProcess::GetCurrentProcessId());
				}

				BatchRequest.BeginObject(ANSITEXTVIEW("Params"));
				{
					ECachePolicy BatchDefaultPolicy = Batch[0].Request.Policy.GetRecordPolicy();
					BatchRequest << ANSITEXTVIEW("DefaultPolicy") << *WriteToString<128>(BatchDefaultPolicy);
					BatchRequest.AddString(ANSITEXTVIEW("Namespace"), CacheStore.Namespace);

					BatchRequest.BeginArray(ANSITEXTVIEW("Requests"));
					for (const TRequestWithStats<FCacheGetRequest>& RequestWithStats : Batch)
					{
						FRequestTimer RequestTimer(RequestWithStats.Stats);
						BatchRequest.BeginObject();
						{
							BatchRequest << ANSITEXTVIEW("Key") << RequestWithStats.Request.Key;

							if ((!RequestWithStats.Request.Policy.IsUniform()) || (RequestWithStats.Request.Policy.GetRecordPolicy() != BatchDefaultPolicy))
							{
								BatchRequest << ANSITEXTVIEW("Policy") << RequestWithStats.Request.Policy;
							}
						}
						BatchRequest.EndObject();
					}
					BatchRequest.EndArray();
				}
				BatchRequest.EndObject();
			}
			BatchRequest.EndObject();

			FGetOp* OriginalOp = this;
			auto OnRpcComplete = [this, OpRef = TRefCountPtr<FGetOp>(OriginalOp), Batch](const THttpUniquePtr<IHttpResponse>& HttpResponse, const FCbPackage& Response, FStringView ResponseText)
			{
				const bool bCanceled = !HttpResponse || HttpResponse->GetErrorCode() == EHttpErrorCode::Canceled;

				int32 RequestIndex = 0;
				if (!bCanceled && HttpResponse->GetErrorCode() == EHttpErrorCode::None && HttpResponse->GetStatusCode() >= 200 && HttpResponse->GetStatusCode() <= 299)
				{
					const FCbObject& ResponseObj = Response.GetObject();

					FCbFieldViewIterator IncompleteFieldIt = ResponseObj[ANSITEXTVIEW("Incomplete")].AsArrayView().CreateViewIterator();
					for (FCbField RecordField : ResponseObj[ANSITEXTVIEW("Result")])
					{
						if (RequestIndex >= Batch.Num())
						{
							++IncompleteFieldIt;
							++RequestIndex;
							continue;
						}

						const TRequestWithStats<FCacheGetRequest>& RequestWithStats = Batch[RequestIndex++];
						RequestWithStats.Stats.Latency = FMonotonicTimeSpan::FromSeconds(HttpResponse->GetStats().GetLatency());

						const FCacheKey& Key = RequestWithStats.Request.Key;
						FOptionalCacheRecord Record;

						if (!RecordField.IsNull())
						{
							Record = FCacheRecord::Load(Response, RecordField.AsObject());
						}
						Record ? OnHit(RequestWithStats, MoveTemp(Record).Get(), IncompleteFieldIt.AsBool()) : OnMiss(RequestWithStats);
						++IncompleteFieldIt;
					}
					if (RequestIndex != Batch.Num())
					{
						UE_LOGF(LogDerivedDataCache, Display,
							"%ls: Invalid response received from GetCacheRecords RPC: %d results expected, received %d, from %ls",
							*CacheStore.GetName(), Batch.Num(), RequestIndex, *WriteToString<256>(*HttpResponse));
					}
				}
				else if (!bCanceled && (HttpResponse->GetStatusCode() != 404))
				{
					UE_LOGF(LogDerivedDataCache, Display,
						"%ls: Error response received from GetCacheRecords RPC: from %ls.\"%.*ls\"",
						*CacheStore.GetName(), *WriteToString<256>(*HttpResponse), ResponseText.Len(), ResponseText.GetData());
				}
					
				for (const TRequestWithStats<FCacheGetRequest>& RequestWithStats : Batch.RightChop(RequestIndex))
				{
					if (bCanceled)
					{
						OnCanceled(RequestWithStats);
					}
					else
					{
						OnMiss(RequestWithStats);
					}
				}
			};

			CacheStore.EnqueueAsyncRpc(Owner, BatchRequest.Save().AsObject(), MoveTemp(OnRpcComplete));
		});
	}

private:
	void OnHit(const TRequestWithStats<FCacheGetRequest>& RequestWithStats, FCacheRecord&& Record, bool bIncomplete)
	{
		FRequestTimer RequestTimer(RequestWithStats.Stats);
		FCacheRecordBuilder RecordBuilder(RequestWithStats.Request.Key);

		const ECachePolicy RecordPolicy = RequestWithStats.Request.Policy.GetRecordPolicy();
		if (!EnumHasAnyFlags(RecordPolicy, ECachePolicy::SkipMeta))
		{
			if (FCbObject Meta = Record.GetMeta())
			{
				RequestWithStats.Stats.PhysicalReadSize += Meta.GetSize();
				RecordBuilder.SetMeta(MoveTemp(Meta));
			}
		}

		EStatus Status = EStatus::Ok;
		const bool bPartialRecord = EnumHasAnyFlags(RecordPolicy, ECachePolicy::PartialRecord);
		if (bPartialRecord && bIncomplete)
		{
			// The server will indicate that a record is incomplete because when requesting
			// partial records and skipping data, the returned record is identical whether
			// the data for the values is present on the server or not present on the server.
			Status = EStatus::Error;
		}
		for (const FValueWithId& Value : Record.GetValues())
		{
			const FValueId& Id = Value.GetId();
			const ECachePolicy ValuePolicy = RequestWithStats.Request.Policy.GetValuePolicy(Value.GetId());
			FValue Content;
			if (CacheStore.ValidateAndPrepareReceivedValue(RequestWithStats.Request.Name,
				RequestWithStats.Request.Key,
				Id,
				Value,
				ValuePolicy,
				Content,
				RequestWithStats.Stats))
			{
				RecordBuilder.AddValue(Id, MoveTemp(Content));
			}
			else if (bPartialRecord)
			{
				Status = EStatus::Error;
				RecordBuilder.AddValue(Value);
			}
			else
			{
				// End immediately with error and no record
				RequestTimer.Stop();
				RequestWithStats.EndRequest(CacheStore, EStatus::Error);
				OnComplete(RequestWithStats.Request.MakeResponse(EStatus::Error));
				return;
			}
		}

		if (Status == EStatus::Ok)
		{
			UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache hit for '%ls' from '%ls'",
				*CacheStore.GetName(), *WriteToString<96>(RequestWithStats.Request.Key), *RequestWithStats.Request.Name);
		}

		RequestTimer.Stop();
		RequestWithStats.EndRequest(CacheStore, Status);

		TRACE_COUNTER_INCREMENT(ZenDDC_GetHit);
		TRACE_COUNTER_ADD(ZenDDC_BytesReceived, int64(RequestWithStats.Stats.PhysicalReadSize));
		OnComplete({RequestWithStats.Request.Name, RecordBuilder.Build(), RequestWithStats.Request.UserData, Status});
	}

	void OnMiss(const TRequestWithStats<FCacheGetRequest>& RequestWithStats)
	{
		FRequestTimer RequestTimer(RequestWithStats.Stats);
		UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache miss for '%ls' from '%ls'",
			*CacheStore.GetName(), *WriteToString<96>(RequestWithStats.Request.Key), *RequestWithStats.Request.Name);

		RequestTimer.Stop();
		RequestWithStats.EndRequest(CacheStore, EStatus::Error);
		OnComplete(RequestWithStats.Request.MakeResponse(EStatus::Error));
	}

	void OnCanceled(const TRequestWithStats<FCacheGetRequest>& RequestWithStats)
	{
		FRequestTimer RequestTimer(RequestWithStats.Stats);
		UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache miss with canceled request for '%ls' from '%ls'",
			*CacheStore.GetName(), *WriteToString<96>(RequestWithStats.Request.Key), *RequestWithStats.Request.Name);

		RequestTimer.Stop();
		RequestWithStats.EndRequest(CacheStore, EStatus::Canceled);
		OnComplete(RequestWithStats.Request.MakeResponse(EStatus::Canceled));
	}

	FZenCacheStore& CacheStore;
	IRequestOwner& Owner;
	TArray<TRequestWithStats<FCacheGetRequest>, TInlineAllocator<1>> Requests;
	FOnCacheGetComplete OnComplete;
};

class FZenCacheStore::FPutValueOp final : public FRefCountedObject
{
public:
	FPutValueOp(FZenCacheStore& InCacheStore,
		IRequestOwner& InOwner,
		const TConstArrayView<FCachePutValueRequest> InRequests,
		FOnCachePutValueComplete&& InOnComplete)
		: CacheStore(InCacheStore)
		, Owner(InOwner)
		, OnComplete(MoveTemp(InOnComplete))
	{
		StartRequests(Requests, InRequests, [](const FCachePutValueRequest& Request) { return Request.Key.Bucket; },
			[](auto&) { return ERequestType::Value; }, ERequestOp::Put);
		TRACE_COUNTER_ADD(ZenDDC_Put, int64(Requests.Num()));
		Batches = TBatchView<const TRequestWithStats<FCachePutValueRequest>>(Requests,
			[this](const TRequestWithStats<FCachePutValueRequest>& NextRequest) { return BatchGroupingFilter(NextRequest.Request); });
	}

	virtual ~FPutValueOp()
	{
		CacheStore.ConditionalEvaluatePerformance();
		CacheStore.ConditionalUpdateStorageSize();
	}

	void IssueRequests()
	{
		FRequestBarrier Barrier(Owner);
		for (TArrayView<const TRequestWithStats<FCachePutValueRequest>> Batch : Batches)
		{
			FCbPackage BatchPackage;
			FCbWriter BatchWriter;
			BatchWriter.BeginObject();
			{
				BatchWriter << ANSITEXTVIEW("Method") << ANSITEXTVIEW("PutCacheValues");
				BatchWriter.AddInteger(ANSITEXTVIEW("Accept"), Zen::Http::kCbPkgMagic);

				BatchWriter.BeginObject(ANSITEXTVIEW("Params"));
				{
					ECachePolicy BatchDefaultPolicy = Batch[0].Request.Policy;
					BatchWriter << ANSITEXTVIEW("DefaultPolicy") << *WriteToString<128>(BatchDefaultPolicy);
					BatchWriter.AddString(ANSITEXTVIEW("Namespace"), CacheStore.Namespace);

					BatchWriter.BeginArray("Requests");
					for (const TRequestWithStats<FCachePutValueRequest>& RequestWithStats : Batch)
					{
						FRequestTimer RequestTimer(RequestWithStats.Stats);
						BatchWriter.BeginObject();
						{
							BatchWriter << ANSITEXTVIEW("Key") << RequestWithStats.Request.Key;
							const FValue& Value = RequestWithStats.Request.Value;
							BatchWriter.AddBinaryAttachment("RawHash", Value.GetRawHash());
							if (Value.HasData())
							{
								BatchPackage.AddAttachment(FCbAttachment(Value.GetData()));
							}
							if (RequestWithStats.Request.Policy != BatchDefaultPolicy)
							{
								BatchWriter << ANSITEXTVIEW("Policy") << WriteToString<128>(RequestWithStats.Request.Policy);
							}
						}
						BatchWriter.EndObject();
					}
					BatchWriter.EndArray();
				}
				BatchWriter.EndObject();
			}
			BatchWriter.EndObject();
			BatchPackage.SetObject(BatchWriter.Save().AsObject());

			auto OnRpcComplete = [this, OpRef = TRefCountPtr<FPutValueOp>(this), Batch](const THttpUniquePtr<IHttpResponse>& HttpResponse, const FCbPackage& Response, FStringView ResponseText)
			{
				const bool bCanceled = !HttpResponse || HttpResponse->GetErrorCode() == EHttpErrorCode::Canceled;

				int32 RequestIndex = 0;
				if (!bCanceled && HttpResponse->GetErrorCode() == EHttpErrorCode::None && HttpResponse->GetStatusCode() >= 200 && HttpResponse->GetStatusCode() <= 299)
				{
					const FCbObject& ResponseObj = Response.GetObject();
					FCbFieldViewIterator DetailsFieldIt = ResponseObj[ANSITEXTVIEW("Details")].AsArrayView().CreateViewIterator();
					for (FCbField ResponseField : ResponseObj[ANSITEXTVIEW("Result")])
					{
						if (RequestIndex >= Batch.Num())
						{
							++DetailsFieldIt;
							++RequestIndex;
							continue;
						}

						const TRequestWithStats<FCachePutValueRequest>& RequestWithStats = Batch[RequestIndex++];

						// Latency can't be measured for Put operations because it is intertwined with upload time.
						RequestWithStats.Stats.Latency = FMonotonicTimeSpan::Infinity();

						const bool bPutSucceeded = ResponseField.AsBool();
						bPutSucceeded ? OnHit(RequestWithStats, DetailsFieldIt.AsObjectView()) : OnMiss(RequestWithStats, DetailsFieldIt.AsObjectView());
						++DetailsFieldIt;
					}
					if (RequestIndex != Batch.Num())
					{
						UE_LOGF(LogDerivedDataCache, Display,
							"%ls: Invalid response received from PutCacheValues RPC: %d results expected, received %d, from %ls",
							*CacheStore.GetName(), Batch.Num(), RequestIndex, *WriteToString<256>(*HttpResponse));
					}
				}
				else if (!bCanceled && (HttpResponse->GetStatusCode() != 404))
				{
					UE_LOGF(LogDerivedDataCache, Display,
						"%ls: Error response received from PutCacheValues RPC: from %ls. \"%.*ls\"",
						*CacheStore.GetName(), *WriteToString<256>(*HttpResponse), ResponseText.Len(), ResponseText.GetData());
				}

				for (const TRequestWithStats<FCachePutValueRequest>& RequestWithStats : Batch.RightChop(RequestIndex))
				{
					if (bCanceled)
					{
						OnCanceled(RequestWithStats);
					}
					else
					{
						OnMiss(RequestWithStats, FCbObjectView());
					}
				}
			};

			CacheStore.EnqueueAsyncRpc(Owner, BatchPackage, MoveTemp(OnRpcComplete));
		}
	}

private:
	EBatchView BatchGroupingFilter(const FCachePutValueRequest& NextRequest)
	{
		uint64 ValueSize = sizeof(FCacheKey) + NextRequest.Value.GetData().GetCompressedSize();
		BatchSize += ValueSize;
		if (BatchSize > CacheStore.MaxBatchPutKB*1024)
		{
			BatchSize = ValueSize;
			return EBatchView::NewBatch;
		}
		return EBatchView::Continue;
	}

	void OnHit(const TRequestWithStats<FCachePutValueRequest>& RequestWithStats, FCbObjectView DetailsObjectView)
	{
		FRequestTimer RequestTimer(RequestWithStats.Stats);

		RequestWithStats.Stats.AddLogicalWrite(RequestWithStats.Request.Value);
		RequestWithStats.Stats.PhysicalWriteSize += RequestWithStats.Request.Value.GetData().GetCompressedSize();

		TRACE_COUNTER_INCREMENT(ZenDDC_PutHit);
		TRACE_COUNTER_ADD(ZenDDC_BytesSent, RequestWithStats.Stats.PhysicalWriteSize);

		FCachePutValueResponse Response = RequestWithStats.Request.MakeResponse(EStatus::Ok);

		if (DetailsObjectView)
		{
			const FValue& Value = RequestWithStats.Request.Value;
			const FIoHash RawHash = DetailsObjectView["RawHash"].AsHash();
			const uint64 RawSize = DetailsObjectView["RawSize"].AsUInt64(MAX_uint64);

			UE_CLOGF(!EnumHasAnyFlags(RequestWithStats.Request.Policy, ECachePolicy::NonDeterministic),
				LogDerivedDataCache, Display, "%ls: Cache put found non-deterministic value "
				"with new hash %ls and existing hash %ls for %ls from '%ls'",
				*CacheStore.GetName(), *WriteToString<48>(Value.GetRawHash()), *WriteToString<48>(RawHash),
				*WriteToString<96>(RequestWithStats.Request.Key), *RequestWithStats.Request.Name);

			Response.Value = FValue(RawHash, RawSize);
		}

		if (!EnumHasAnyFlags(RequestWithStats.Request.Policy, ECachePolicy::SkipData) && !Response.Value.HasData())
		{
			RequestTimer.Stop();
			ForwardAsGet(RequestWithStats);
			return;
		}

		UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache put complete for %ls from '%ls'",
			*CacheStore.GetName(), *WriteToString<96>(RequestWithStats.Request.Key), *RequestWithStats.Request.Name);

		RequestTimer.Stop();
		RequestWithStats.EndRequest(CacheStore, EStatus::Ok);
		OnComplete(MoveTemp(Response));
	}

	void OnMiss(const TRequestWithStats<FCachePutValueRequest>& RequestWithStats, FCbObjectView DetailsObjectView)
	{
		FRequestTimer RequestTimer(RequestWithStats.Stats);
		FUtf8StringView DetailsMessage = DetailsObjectView[ANSITEXTVIEW("Message")].AsString();
		if (DetailsObjectView && !DetailsMessage.IsEmpty())
		{
			UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache put failed for '%ls' from '%ls' with message '%ls'",
				*CacheStore.GetName(), *WriteToString<96>(RequestWithStats.Request.Key), *RequestWithStats.Request.Name, *WriteToString<64>(DetailsMessage));
		}
		else
		{
			UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache put failed for '%ls' from '%ls'",
				*CacheStore.GetName(), *WriteToString<96>(RequestWithStats.Request.Key), *RequestWithStats.Request.Name);
		}

		RequestTimer.Stop();
		RequestWithStats.EndRequest(CacheStore, EStatus::Error);
		OnComplete(RequestWithStats.Request.MakeResponse(EStatus::Error));
	}

	void OnCanceled(const TRequestWithStats<FCachePutValueRequest>& RequestWithStats)
	{
		FRequestTimer RequestTimer(RequestWithStats.Stats);
		UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache put failed with canceled request for '%ls' from '%ls'",
			*CacheStore.GetName(), *WriteToString<96>(RequestWithStats.Request.Key), *RequestWithStats.Request.Name);

		RequestTimer.Stop();
		RequestWithStats.EndRequest(CacheStore, EStatus::Canceled);
		OnComplete(RequestWithStats.Request.MakeResponse(EStatus::Canceled));
	}

	void ForwardAsGet(const TRequestWithStats<FCachePutValueRequest>& RequestWithStats);

	FZenCacheStore& CacheStore;
	IRequestOwner& Owner;
	TArray<TRequestWithStats<FCachePutValueRequest>, TInlineAllocator<1>> Requests;
	uint64 BatchSize = 0;
	TBatchView<const TRequestWithStats<FCachePutValueRequest>> Batches;
	FOnCachePutValueComplete OnComplete;
};

class FZenCacheStore::FGetValueOp final : public FRefCountedObject
{
public:
	FGetValueOp(FZenCacheStore& InCacheStore,
		IRequestOwner& InOwner,
		const TConstArrayView<FCacheGetValueRequest> InRequests,
		FOnCacheGetValueComplete&& InOnComplete)
		: CacheStore(InCacheStore)
		, Owner(InOwner)
		, OnComplete(MoveTemp(InOnComplete))
	{
		StartRequests(Requests, InRequests, [](const FCacheGetValueRequest& Request) { return Request.Key.Bucket; },
			[](auto&) { return ERequestType::Value; }, ERequestOp::Get);
		TRACE_COUNTER_ADD(ZenDDC_Get, (int64)Requests.Num());
		TRACE_COUNTER_ADD(ZenDDC_CacheRecordRequestCountInFlight, int64(Requests.Num()));
	}

	virtual ~FGetValueOp()
	{
		TRACE_COUNTER_SUBTRACT(ZenDDC_CacheRecordRequestCountInFlight, int64(Requests.Num()));

		CacheStore.ConditionalEvaluatePerformance();
		CacheStore.ConditionalUpdateStorageSize();
	}

	void IssueRequests()
	{
		FRequestBarrier Barrier(Owner);
		ForEachBatch(CacheStore.CacheRecordBatchSize, Requests.Num(),
			[this](int32 BatchFirst, int32 BatchLast)
		{
			TConstArrayView<TRequestWithStats<FCacheGetValueRequest>> Batch(Requests.GetData() + BatchFirst, BatchLast - BatchFirst + 1);

			FCbWriter BatchRequest;
			BatchRequest.BeginObject();
			{
				BatchRequest << ANSITEXTVIEW("Method") << ANSITEXTVIEW("GetCacheValues");
				BatchRequest.AddInteger(ANSITEXTVIEW("Accept"), Zen::Http::kCbPkgMagic);
				if (CacheStore.bIsLocalConnection)
				{
					BatchRequest.AddInteger(ANSITEXTVIEW("AcceptFlags"), static_cast<uint32_t>(Zen::Http::RpcAcceptOptions::kAllowLocalReferences));
					BatchRequest.AddInteger(ANSITEXTVIEW("Pid"), FPlatformProcess::GetCurrentProcessId());
				}

				BatchRequest.BeginObject(ANSITEXTVIEW("Params"));
				{
					ECachePolicy BatchDefaultPolicy = Batch[0].Request.Policy;
					BatchRequest << ANSITEXTVIEW("DefaultPolicy") << *WriteToString<128>(BatchDefaultPolicy);
					BatchRequest.AddString(ANSITEXTVIEW("Namespace"), CacheStore.Namespace);

					BatchRequest.BeginArray("Requests");
					for (const TRequestWithStats<FCacheGetValueRequest>& RequestWithStats : Batch)
					{
						FRequestTimer RequestTimer(RequestWithStats.Stats);
						BatchRequest.BeginObject();
						{
							BatchRequest << ANSITEXTVIEW("Key") << RequestWithStats.Request.Key;
							if (RequestWithStats.Request.Policy != BatchDefaultPolicy)
							{
								BatchRequest << ANSITEXTVIEW("Policy") << WriteToString<128>(RequestWithStats.Request.Policy);
							}
						}
						BatchRequest.EndObject();
					}
					BatchRequest.EndArray();
				}
				BatchRequest.EndObject();
			}
			BatchRequest.EndObject();

			FGetValueOp* OriginalOp = this;
			auto OnRpcComplete = [this, OpRef = TRefCountPtr<FGetValueOp>(OriginalOp), Batch](const THttpUniquePtr<IHttpResponse>& HttpResponse, const FCbPackage& Response, FStringView ResponseText)
			{
				const bool bCanceled = !HttpResponse || HttpResponse->GetErrorCode() == EHttpErrorCode::Canceled;

				int32 RequestIndex = 0;
				if (!bCanceled && HttpResponse->GetErrorCode() == EHttpErrorCode::None && HttpResponse->GetStatusCode() >= 200 && HttpResponse->GetStatusCode() <= 299)
				{
					const FCbObject& ResponseObj = Response.GetObject();

					for (FCbFieldView ResultField : ResponseObj[ANSITEXTVIEW("Result")])
					{
						if (RequestIndex >= Batch.Num())
						{
							++RequestIndex;
							continue;
						}

						const TRequestWithStats<FCacheGetValueRequest>& RequestWithStats = Batch[RequestIndex++];
						RequestWithStats.Stats.Latency = FMonotonicTimeSpan::FromSeconds(HttpResponse->GetStats().GetLatency());

						FCbObjectView ResultObj = ResultField.AsObjectView();
						TOptional<FValue> Value;
						if (FIoHash RawHash = ResultObj[ANSITEXTVIEW("RawHash")].AsHash(); !RawHash.IsZero())
						{
							if (EnumHasAnyFlags(RequestWithStats.Request.Policy, ECachePolicy::SkipData))
							{
								if (uint64 RawSize = ResultObj[ANSITEXTVIEW("RawSize")].AsUInt64(MAX_uint64); RawSize != MAX_uint64)
								{
									Value.Emplace(RawHash, RawSize);
								}
							}
							else
							{
								if (const FCbAttachment* Attachment = Response.FindAttachment(RawHash))
								{
									const FCompressedBuffer& CompressedData = Attachment->AsCompressedBinary();
									if (CompressedData.GetRawHash() == RawHash)
									{
										Value.Emplace(CompressedData);
									}
								}
							}
						}
						(bool)Value ? OnHit(RequestWithStats, MoveTemp(*Value)) : OnMiss(RequestWithStats);
					}
					if (RequestIndex != Batch.Num())
					{
						UE_LOGF(LogDerivedDataCache, Display,
							"%ls: Invalid response received from GetCacheValues RPC: %d results expected, received %d from %ls",
							*CacheStore.GetName(), Batch.Num(), RequestIndex, *WriteToString<256>(*HttpResponse));
					}
				}
				else if (!bCanceled && (HttpResponse->GetStatusCode() != 404))
				{
					UE_LOGF(LogDerivedDataCache, Display,
						"%ls: Error response received from GetCacheValues RPC: from %ls. \"%.*ls\"",
						*CacheStore.GetName(), *WriteToString<256>(*HttpResponse), ResponseText.Len(), ResponseText.GetData());
				}

				for (const TRequestWithStats<FCacheGetValueRequest>& RequestWithStats : Batch.RightChop(RequestIndex))
				{
					if (bCanceled)
					{
						OnCanceled(RequestWithStats);
					}
					else
					{
						OnMiss(RequestWithStats);
					}
				}
			};

			CacheStore.EnqueueAsyncRpc(Owner, BatchRequest.Save().AsObject(), MoveTemp(OnRpcComplete));
		});
	}

private:
	void OnHit(const TRequestWithStats<FCacheGetValueRequest>& RequestWithStats, FValue&& Value)
	{
		FRequestTimer RequestTimer(RequestWithStats.Stats);
		if (!CacheStore.ValidateAndPrepareReceivedValue(RequestWithStats.Request.Name,
			RequestWithStats.Request.Key,
			{},
			Value,
			RequestWithStats.Request.Policy,
			Value,
			RequestWithStats.Stats))
		{
			// End immediately with error and no value
			RequestTimer.Stop();
			RequestWithStats.EndRequest(CacheStore, EStatus::Error);
			OnComplete(RequestWithStats.Request.MakeResponse(EStatus::Error));
			return;
		}

		UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache hit for '%ls' from '%ls'",
			*CacheStore.GetName(), *WriteToString<96>(RequestWithStats.Request.Key), *RequestWithStats.Request.Name);

		RequestTimer.Stop();
		RequestWithStats.EndRequest(CacheStore, EStatus::Ok);

		TRACE_COUNTER_INCREMENT(ZenDDC_GetHit);
		TRACE_COUNTER_ADD(ZenDDC_BytesReceived, int64(RequestWithStats.Stats.PhysicalReadSize));
		OnComplete({RequestWithStats.Request.Name, RequestWithStats.Request.Key, MoveTemp(Value), RequestWithStats.Request.UserData, EStatus::Ok});
	};

	void OnMiss(const TRequestWithStats<FCacheGetValueRequest>& RequestWithStats)
	{
		FRequestTimer RequestTimer(RequestWithStats.Stats);
		UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache miss for '%ls' from '%ls'",
			*CacheStore.GetName(), *WriteToString<96>(RequestWithStats.Request.Key), *RequestWithStats.Request.Name);

		RequestTimer.Stop();
		RequestWithStats.EndRequest(CacheStore, EStatus::Error);
		OnComplete(RequestWithStats.Request.MakeResponse(EStatus::Error));
	};

	void OnCanceled(const TRequestWithStats<FCacheGetValueRequest>& RequestWithStats)
	{
		FRequestTimer RequestTimer(RequestWithStats.Stats);
		UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache miss with canceled request for '%ls' from '%ls'",
			*CacheStore.GetName(), *WriteToString<96>(RequestWithStats.Request.Key), *RequestWithStats.Request.Name);

		RequestTimer.Stop();
		RequestWithStats.EndRequest(CacheStore, EStatus::Canceled);
		OnComplete(RequestWithStats.Request.MakeResponse(EStatus::Canceled));
	};

	FZenCacheStore& CacheStore;
	IRequestOwner& Owner;
	TArray<TRequestWithStats<FCacheGetValueRequest>, TInlineAllocator<1>> Requests;
	FOnCacheGetValueComplete OnComplete;
};

class FZenCacheStore::FGetChunksOp final : public FRefCountedObject
{
public:
	FGetChunksOp(FZenCacheStore& InCacheStore,
		IRequestOwner& InOwner,
		const TConstArrayView<FCacheGetChunkRequest> InRequests,
		FOnCacheGetChunkComplete&& InOnComplete)
		: CacheStore(InCacheStore)
		, Owner(InOwner)
		, OnComplete(MoveTemp(InOnComplete))
	{
		StartRequests(Requests, InRequests, [](const FCacheGetChunkRequest& Request) { return Request.Key.Bucket; },
			[](const FCacheGetChunkRequest& Request) { return Request.Id.IsNull() ? ERequestType::Value : ERequestType::Record; },
			ERequestOp::GetChunk);
		Algo::StableSortBy(Requests, &TRequestWithStats<FCacheGetChunkRequest>::Request, TChunkLess());
		TRACE_COUNTER_ADD(ZenDDC_Get, int64(Requests.Num()));
		TRACE_COUNTER_ADD(ZenDDC_ChunkRequestCountInFlight, int64(Requests.Num()));
	}

	virtual ~FGetChunksOp()
	{
		TRACE_COUNTER_SUBTRACT(ZenDDC_ChunkRequestCountInFlight, int64(Requests.Num()));

		CacheStore.ConditionalEvaluatePerformance();
		CacheStore.ConditionalUpdateStorageSize();
	}

	void IssueRequests()
	{
		FRequestBarrier Barrier(Owner);
		ForEachBatch(CacheStore.CacheChunksBatchSize, Requests.Num(),
			[this](int32 BatchFirst, int32 BatchLast)
		{
			TConstArrayView<TRequestWithStats<FCacheGetChunkRequest>> Batch(Requests.GetData() + BatchFirst, BatchLast - BatchFirst + 1);

			FCbWriter BatchRequest;
			BatchRequest.BeginObject();
			{
				BatchRequest << ANSITEXTVIEW("Method") << "GetCacheChunks";
				BatchRequest.AddInteger(ANSITEXTVIEW("Accept"), Zen::Http::kCbPkgMagic);
				uint32_t AcceptFlags = static_cast<uint32_t>(Zen::Http::RpcAcceptOptions::kAllowPartialCacheChunks);
				if (CacheStore.bIsLocalConnection)
				{
					AcceptFlags |= static_cast<uint32_t>(Zen::Http::RpcAcceptOptions::kAllowLocalReferences);
					BatchRequest.AddInteger(ANSITEXTVIEW("Pid"), FPlatformProcess::GetCurrentProcessId());
				}
				BatchRequest.AddInteger(ANSITEXTVIEW("AcceptFlags"), AcceptFlags);

				BatchRequest.BeginObject(ANSITEXTVIEW("Params"));
				{
					ECachePolicy DefaultPolicy = Batch[0].Request.Policy;
					BatchRequest << ANSITEXTVIEW("DefaultPolicy") << WriteToString<128>(DefaultPolicy);
					BatchRequest.AddString(ANSITEXTVIEW("Namespace"), CacheStore.Namespace);

					BatchRequest.BeginArray(ANSITEXTVIEW("ChunkRequests"));
					for (const TRequestWithStats<FCacheGetChunkRequest>& RequestWithStats : Batch)
					{
						FRequestTimer RequestTimer(RequestWithStats.Stats);
						const FCacheGetChunkRequest& Request = RequestWithStats.Request;
						BatchRequest.BeginObject();
						{
							BatchRequest << ANSITEXTVIEW("Key") << Request.Key;

							if (Request.Id.IsValid())
							{
								BatchRequest.AddObjectId(ANSITEXTVIEW("ValueId"), Request.Id);
							}
							if (Request.RawOffset != 0)
							{
								BatchRequest << ANSITEXTVIEW("RawOffset") << Request.RawOffset;
							}
							if (Request.RawSize != MAX_uint64)
							{
								BatchRequest << ANSITEXTVIEW("RawSize") << Request.RawSize;
							}
							if (!Request.RawHash.IsZero())
							{
								BatchRequest << ANSITEXTVIEW("ChunkId") << Request.RawHash;
							}
							if (Request.Policy != DefaultPolicy)
							{
								BatchRequest << ANSITEXTVIEW("Policy") << WriteToString<128>(Request.Policy);
							}
						}
						BatchRequest.EndObject();
					}
					BatchRequest.EndArray();
				}
				BatchRequest.EndObject();
			}
			BatchRequest.EndObject();

			FGetChunksOp* OriginalOp = this;
			auto OnRpcComplete = [this, OpRef = TRefCountPtr<FGetChunksOp>(OriginalOp), Batch](const THttpUniquePtr<IHttpResponse>& HttpResponse, const FCbPackage& Response, FStringView ResponseText)
			{
				const bool bCanceled = !HttpResponse || HttpResponse->GetErrorCode() == EHttpErrorCode::Canceled;

				int32 RequestIndex = 0;
				if (!bCanceled && HttpResponse->GetErrorCode() == EHttpErrorCode::None && HttpResponse->GetStatusCode() >= 200 && HttpResponse->GetStatusCode() <= 299)
				{
					const FCbObject& ResponseObj = Response.GetObject();

					for (FCbFieldView ResultView : ResponseObj[ANSITEXTVIEW("Result")])
					{
						if (RequestIndex >= Batch.Num())
						{
							++RequestIndex;
							continue;
						}
						const TRequestWithStats<FCacheGetChunkRequest>& RequestWithStats = Batch[RequestIndex++];
						RequestWithStats.Stats.Latency = FMonotonicTimeSpan::FromSeconds(HttpResponse->GetStats().GetLatency());

						bool bSucceeded = false;
						FIoHash RawHash;
						uint64 RawSize = 0;
						FSharedBuffer RequestedBytes;
						const FCacheGetChunkRequest& Request = RequestWithStats.Request;
						if (RawHash = ResultView[ANSITEXTVIEW("RawHash")].AsHash(); !RawHash.IsZero())
						{
							if (EnumHasAnyFlags(RequestWithStats.Request.Policy, ECachePolicy::SkipData))
							{
								if (uint64 TotalSize = ResultView[ANSITEXTVIEW("RawSize")].AsUInt64(MAX_uint64); TotalSize != MAX_uint64)
								{
									RawSize = FMath::Min(Request.RawSize, TotalSize - FMath::Min(Request.RawOffset, TotalSize));
									bSucceeded = true;
								}
							}
							else
							{
								const FIoHash FragmentHash = ResultView[ANSITEXTVIEW("FragmentHash")].AsHash();
								const uint64 FragmentOffset = ResultView[ANSITEXTVIEW("FragmentOffset")].AsUInt64(0);
								const bool bFragment = !FragmentHash.IsZero();
								if (const FCbAttachment* Attachment = Response.FindAttachment(bFragment ? FragmentHash : RawHash))
								{
									const FCompressedBuffer& CompressedData = Attachment->AsCompressedBinary();
									if (CompressedData && (bFragment || CompressedData.GetRawHash() == RawHash))
									{
										const uint64 OffsetInFragment = Request.RawOffset - FragmentOffset;
										RawSize = FMath::Min(CompressedData.GetRawSize() - OffsetInFragment, Request.RawSize);
										RequestedBytes = FCompressedBufferReader(CompressedData).Decompress(OffsetInFragment, RawSize);
										bSucceeded = RequestedBytes.GetSize() == RawSize;
									}
								}
								else if (!bFragment)
								{
									// An out-of-bounds access will respond with no buffer.
									RequestedBytes = FUniqueBuffer::Alloc(0).MoveToShared();
									bSucceeded = true;
								}
							}
						}
						bSucceeded ? OnHit(RequestWithStats, MoveTemp(RawHash), RawSize, MoveTemp(RequestedBytes)) : OnMiss(RequestWithStats);
					}
				}
				else if (!bCanceled && (HttpResponse->GetStatusCode() != 404))
				{
					UE_LOGF(LogDerivedDataCache, Display,
						"%ls: Error response received from GetChunks RPC: from %ls. \"%.*ls\"",
						*CacheStore.GetName(), *WriteToString<256>(*HttpResponse), ResponseText.Len(), ResponseText.GetData());
				}

				for (const TRequestWithStats<FCacheGetChunkRequest>& RequestWithStats : Batch.RightChop(RequestIndex))
				{
					if (bCanceled)
					{
						OnCanceled(RequestWithStats);
					}
					else
					{
						OnMiss(RequestWithStats);
					}
				}
			};
			CacheStore.EnqueueAsyncRpc(Owner, BatchRequest.Save().AsObject(), MoveTemp(OnRpcComplete));
		});
	}

private:
	void OnHit(const TRequestWithStats<FCacheGetChunkRequest>& RequestWithStats, FIoHash&& RawHash, uint64 RawSize, FSharedBuffer&& RequestedBytes)
	{
		FRequestTimer RequestTimer(RequestWithStats.Stats);
		const FCacheGetChunkRequest& Request = RequestWithStats.Request;
		UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache hit for '%ls' from '%ls'",
			*CacheStore.GetName(), *WriteToString<96>(Request.Key, '/', Request.Id), *Request.Name);

		// This is a rough estimate of physical read size until Zen communicates stats with each response.
		RequestWithStats.Stats.LogicalReadSize += RawSize;
		RequestWithStats.Stats.PhysicalReadSize += RequestedBytes.GetSize();

		RequestTimer.Stop();
		RequestWithStats.EndRequest(CacheStore, EStatus::Ok);

		TRACE_COUNTER_INCREMENT(ZenDDC_GetHit);
		TRACE_COUNTER_ADD(ZenDDC_BytesReceived, int64(RequestWithStats.Stats.PhysicalReadSize));
		OnComplete({Request.Name, Request.Key, Request.Id, Request.RawOffset,
			RawSize, MoveTemp(RawHash), MoveTemp(RequestedBytes), Request.UserData, EStatus::Ok});
	};

	void OnMiss(const TRequestWithStats<FCacheGetChunkRequest>& RequestWithStats)
	{
		FRequestTimer RequestTimer(RequestWithStats.Stats);
		const FCacheGetChunkRequest& Request = RequestWithStats.Request;
		UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache miss with missing value '%ls' for '%ls' from '%ls'",
			*CacheStore.GetName(), *WriteToString<16>(Request.Id), *WriteToString<96>(Request.Key), *Request.Name);

		RequestTimer.Stop();
		RequestWithStats.EndRequest(CacheStore, EStatus::Error);
		OnComplete(Request.MakeResponse(EStatus::Error));
	};

	void OnCanceled(const TRequestWithStats<FCacheGetChunkRequest>& RequestWithStats)
	{
		FRequestTimer RequestTimer(RequestWithStats.Stats);
		const FCacheGetChunkRequest& Request = RequestWithStats.Request;
		UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache miss with canceled request for '%ls' from '%ls'",
			*CacheStore.GetName(), *WriteToString<96>(Request.Key, '/', Request.Id), *Request.Name);

		RequestTimer.Stop();
		RequestWithStats.EndRequest(CacheStore, EStatus::Canceled);
		OnComplete(Request.MakeResponse(EStatus::Canceled));
	};

	FZenCacheStore& CacheStore;
	IRequestOwner& Owner;
	TArray<TRequestWithStats<FCacheGetChunkRequest>, TInlineAllocator<1>> Requests;
	FOnCacheGetChunkComplete OnComplete;
};

void FZenCacheStore::FPutOp::ForwardAsGet(const TRequestWithStats<FCachePutRequest>& RequestWithStats)
{
	const FCacheRecordPolicy Policy = RequestWithStats.Request.Policy.Transform([](ECachePolicy P) { return P | ECachePolicy::Query; });
	const FCacheGetRequest Request[]{{RequestWithStats.Request.Name, RequestWithStats.Request.Record.GetKey(), Policy, RequestWithStats.Request.UserData}};
	TRefCountPtr<FGetOp> GetOp = CacheStore.MakeAsyncOp<FGetOp>(CacheStore, Owner, MakeArrayView(Request),
		[this, OpRef = TRefCountPtr<FPutOp>(this), &RequestWithStats](FCacheGetResponse&& Response)
		{
			// A put of a partial record will lead to Error if the returned record is partial.
			// Convert Error to Ok if this was a put of a partial record.
			if (Response.Status == EStatus::Error && EnumHasAnyFlags(RequestWithStats.Request.Policy.GetRecordPolicy(), ECachePolicy::PartialRecord))
			{
				Response.Status = EStatus::Ok;
			}

			UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache put %ls for %ls from '%ls'", *CacheStore.GetName(),
				Response.Status == EStatus::Ok ? TEXT("complete") :
				Response.Status == EStatus::Canceled ? TEXT("failed with canceled request") : TEXT("failed"),
				*WriteToString<96>(Response.Record.GetKey()), *Response.Name);

			RequestWithStats.EndRequest(CacheStore, Response.Status);
			PRAGMA_DISABLE_DEPRECATION_WARNINGS;
			OnComplete({Response.Name, Response.Record.GetKey(), Response.Record, Response.UserData, Response.Status});
			PRAGMA_ENABLE_DEPRECATION_WARNINGS;
		});
	GetOp->IssueRequests();
}

void FZenCacheStore::FPutValueOp::ForwardAsGet(const TRequestWithStats<FCachePutValueRequest>& RequestWithStats)
{
	const FCacheGetValueRequest Request[]{{RequestWithStats.Request.Name, RequestWithStats.Request.Key, RequestWithStats.Request.Policy | ECachePolicy::Query, RequestWithStats.Request.UserData}};
	TRefCountPtr<FGetValueOp> GetValueOp = CacheStore.MakeAsyncOp<FGetValueOp>(CacheStore, Owner, MakeArrayView(Request),
		[this, OpRef = TRefCountPtr<FPutValueOp>(this), &RequestWithStats](FCacheGetValueResponse&& Response)
		{
			UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache put %ls for %ls from '%ls'", *CacheStore.GetName(),
				Response.Status == EStatus::Ok ? TEXT("complete") :
				Response.Status == EStatus::Canceled ? TEXT("failed with canceled request") : TEXT("failed"),
				*WriteToString<96>(Response.Key), *Response.Name);

			RequestWithStats.EndRequest(CacheStore, Response.Status);
			OnComplete({Response.Name, Response.Key, Response.Value, Response.UserData, Response.Status});
		});
	GetValueOp->IssueRequests();
}

class FZenCacheStore::FHealthReceiver final : public IHttpReceiver
{
public:
	FHealthReceiver(const FHealthReceiver&) = delete;
	FHealthReceiver& operator=(const FHealthReceiver&) = delete;

	explicit FHealthReceiver(EHealth& OutHealth, FString* OutResponseBody = nullptr, IHttpReceiver* InNext = nullptr)
		: Health(OutHealth)
		, ResponseBody(OutResponseBody)
		, Next(InNext)
	{
		Health = EHealth::Unknown;
	}

private:
	IHttpReceiver* OnCreate(IHttpResponse& Response) final
	{
		return &BodyReceiver;
	}

	IHttpReceiver* OnComplete(IHttpResponse& Response) final
	{
		FUtf8StringView ResponseStringView(reinterpret_cast<const UTF8CHAR*>(BodyArray.GetData()), IntCastChecked<int32>(BodyArray.Num()));
		if (ResponseStringView == UTF8TEXTVIEW("OK!"))
		{
			Health = EHealth::Ok;
		}
		else
		{
			Health = EHealth::Error;
		}

		if (ResponseBody)
		{
			*ResponseBody = *WriteToString<64>(ResponseStringView);
		}
		return Next;
	}

private:
	EHealth& Health;
	FString* ResponseBody;
	IHttpReceiver* Next;
	TArray64<uint8> BodyArray;
	FHttpByteArrayReceiver BodyReceiver{ BodyArray, this };
};

class FZenCacheStore::FAsyncHealthReceiver final : public FRequestBase, public IHttpReceiver
{
public:
	FAsyncHealthReceiver(const FAsyncHealthReceiver&) = delete;
	FAsyncHealthReceiver& operator=(const FAsyncHealthReceiver&) = delete;

	FAsyncHealthReceiver(
		THttpUniquePtr<IHttpRequest>&& InRequest,
		IRequestOwner* InOwner,
		Zen::FZenServiceInstance& InZenServiceInstance,
		FOnHealthComplete&& InOnHealthComplete)
		: Request(MoveTemp(InRequest))
		, Owner(InOwner)
		, ZenServiceInstance(InZenServiceInstance)
		, BaseReceiver(Health, nullptr, this)
		, OnHealthComplete(MoveTemp(InOnHealthComplete))
	{
		Request->SendAsync(this, Response);
	}

private:
	// IRequest Interface

	void SetPriority(EPriority Priority) final {}
	void Cancel() final { Monitor->Cancel(); }
	void Wait() final { Monitor->Wait(); }

	// IHttpReceiver Interface

	IHttpReceiver* OnCreate(IHttpResponse& LocalResponse) final
	{
		Monitor = LocalResponse.GetMonitor();
		Owner->Begin(this);
		return &BaseReceiver;
	}

	bool ShouldRetry(IHttpResponse& LocalResponse)
	{
		if ((LocalResponse.GetErrorCode() == EHttpErrorCode::Connect) ||
			(LocalResponse.GetErrorCode() == EHttpErrorCode::TlsConnect) ||
			(LocalResponse.GetErrorCode() == EHttpErrorCode::TimedOut))
		{
			if (ZenServiceInstance.IsServiceRunningLocally())
			{
				return ZenServiceInstance.TryRecovery(Zen::FZenServiceInstance::ERecoveryMode::Soft);
			}
			return true;
		}

		return false;
	}

	IHttpReceiver* OnComplete(IHttpResponse& LocalResponse) final
	{
		Owner->End(this, [Self = this, &LocalResponse]
		{
			if (Self->ShouldRetry(LocalResponse))
			{
				new FAsyncHealthReceiver(MoveTemp(Self->Request), Self->Owner, Self->ZenServiceInstance, MoveTemp(Self->OnHealthComplete));
				return;
			}

			Self->Request.Reset();
			if (Self->OnHealthComplete)
			{
				// Launch a task for the completion function since it can execute arbitrary code.
				Self->Owner->LaunchTask(TEXT("ZenHealthComplete"), [Self = TRefCountPtr(Self)]
				{
					// Ensuring that the OnRpcComplete method is destroyed by the time we exit this method by moving it to a local scope variable
					FOnHealthComplete LocalOnComplete = MoveTemp(Self->OnHealthComplete);
					LocalOnComplete(Self->Response, Self->Health);
				});
			}
		});
		return nullptr;
	}

private:
	THttpUniquePtr<IHttpRequest> Request;
	THttpUniquePtr<IHttpResponse> Response;
	TRefCountPtr<IHttpResponseMonitor> Monitor;
	IRequestOwner* Owner;
	Zen::FZenServiceInstance& ZenServiceInstance;
	EHealth Health;
	FHealthReceiver BaseReceiver;
	FOnHealthComplete OnHealthComplete;
};

class FZenCacheStore::FRequestAsyncCbPackageReceiver final : public FRequestBase, public Zen::FAsyncCbPackageReceiver
{
public:
	using FOnComplete = TUniqueFunction<void(const THttpUniquePtr<IHttpResponse>& HttpResponse, const FCbPackage& ResponsePackage, FStringView ResponseText)>;

	FRequestAsyncCbPackageReceiver(const FAsyncCbPackageReceiver&) = delete;
	FRequestAsyncCbPackageReceiver& operator=(const FAsyncCbPackageReceiver&) = delete;

	FRequestAsyncCbPackageReceiver(
		THttpUniquePtr<IHttpRequest>&& InRequest,
		IRequestOwner* InOwner,
		Zen::FZenServiceInstance& InZenServiceInstance,
		FOnComplete&& InOnComplete)
		: FAsyncCbPackageReceiver(MoveTemp(InRequest), InZenServiceInstance,
		[Self = this, LocalOnComplete = MoveTemp(InOnComplete)]
		(FAsyncCbPackageReceiver* Receiver) mutable
		{
			Self->Owner->LaunchTask(TEXT("FRequestAsyncCbPackageReceiver::OnComplete"), [Self = TRefCountPtr(Self), InnerOnComplete = MoveTemp(LocalOnComplete)]
			{
				InnerOnComplete(Self->Response, Self->Package, Self->GetPayloadAsString());
			});
		},
		/*MaxAttempts*/ 3,
		/*UseSoftRecovery*/ true)
		, Owner(InOwner)
	{
	}

private:
	// IRequest Interface

	void SetPriority(EPriority Priority) final {}
	void Cancel() final { CopyMonitorRef()->Cancel(); }
	void Wait() final { CopyMonitorRef()->Wait(); }

	// IHttpReceiver Interface

	IHttpReceiver* OnCreate(IHttpResponse& LocalResponse) final
	{
		{
			TUniqueLock Lock(MonitorMutex);
			Monitor = LocalResponse.GetMonitor();
		}
		Owner->Begin(this);
		return FAsyncCbPackageReceiver::OnCreate(LocalResponse);
	}

	IHttpReceiver* OnComplete(IHttpResponse& LocalResponse) final
	{
		Owner->End(this, [Self = this, &LocalResponse]
		{
			Self->FAsyncCbPackageReceiver::OnComplete(LocalResponse);
		});
		return nullptr;
	}

private:
	TRefCountPtr<IHttpResponseMonitor> CopyMonitorRef()
	{
		TUniqueLock Lock(MonitorMutex);
		return Monitor;
	}

	TRefCountPtr<IHttpResponseMonitor> Monitor;
	IRequestOwner* Owner;
	FMutex MonitorMutex;
};

FZenCacheStore::FZenCacheStore(
	const FZenCacheStoreParams& InParams,
	ICacheStoreOwner& InStoreOwner)
	: ZenService(*InParams.Host)
	, StoreOwner(InStoreOwner)
	, PerformanceEvaluationRequestOwner(EPriority::Low)
{
	Initialize(InParams);
}

FZenCacheStore::FZenCacheStore(
	UE::Zen::FServiceSettings&& InSettings,
	const FZenCacheStoreParams& InParams,
	ICacheStoreOwner& InStoreOwner)
	: ZenService(MoveTemp(InSettings))
	, StoreOwner(InStoreOwner)
	, PerformanceEvaluationRequestOwner(EPriority::Low)
{
	Initialize(InParams);
}

FZenCacheStore::~FZenCacheStore()
{
	PerformanceEvaluationRequestOwner.Cancel();
	if (PerformanceEvaluationThread.IsSet())
	{
		PerformanceEvaluationThreadShutdownEvent.Notify();
		PerformanceEvaluationThread->Join();
		PerformanceEvaluationThread.Reset();
		PerformanceEvaluationThreadShutdownEvent.Reset();
	}

	if (StoreStats)
	{
		StoreOwner.DestroyStats(StoreStats);
	}
}

bool FZenCacheStore::ReadyWait(const FZenCacheStoreParams& Params)
{
	const float ReadyWaitMs = GIsBuildMachine ? Params.ReadyWaitBuildMachineMs : Params.ReadyWaitMs;
	const bool bReadyWaitRetry = GIsBuildMachine ? Params.bReadyWaitRetryBuildMachine : Params.bReadyWaitRetry;

	const Zen::FZenServiceEndpoint& ZenServiceEndpoint = ZenService.GetInstance().GetEndpoint();

	bool bReady = false;
	uint64 ReadyWaitStartTime = FPlatformTime::Cycles64();
	TStringBuilder<64> ResultContext;

	while (true)
	{
		uint64 LoopStartTime = FPlatformTime::Cycles64();
		uint32 ReadyWaitMsRemaining = static_cast<uint32>(FMath::Max<double>(0.0, ReadyWaitMs - FPlatformTime::ToMilliseconds64(LoopStartTime - ReadyWaitStartTime)));
		if (ReadyWaitMsRemaining < 1)
		{
			break;
		}

		// Issue a synchronous health/ready request with a limited total time
		FHttpClientParams ReadinessClientParams;
		ReadinessClientParams.Version = EHttpVersion::V2;
		ReadinessClientParams.MaxRequests = 1;
		ReadinessClientParams.MinRequests = 1;

		ReadinessClientParams.TotalTimeoutMs = ReadyWaitMsRemaining;
		ReadinessClientParams.bBypassProxy = Params.bBypassProxy;
		THttpUniquePtr<IHttpClient> ReadinessClient = ConnectionPool->CreateClient(ReadinessClientParams);
		THttpUniquePtr<IHttpRequest> ReadinessRequest = ReadinessClient->TryCreateRequest({});
		SetRequestUri(*ReadinessRequest, ANSITEXTVIEW("/health/ready"));
		ReadinessRequest->SetMethod(EHttpMethod::Get);
		ReadinessRequest->AddAcceptType(EHttpMediaType::Text);
		EHealth Health = EHealth::Unknown;
		FString ResponseString;
		FHealthReceiver HealthReceiver(Health, &ResponseString);
		THttpUniquePtr<IHttpResponse> ReadinessResponse;
		ReadinessRequest->Send(&HealthReceiver, ReadinessResponse);

		if (ReadinessResponse->GetErrorCode() == EHttpErrorCode::None &&
			(ReadinessResponse->GetStatusCode() >= 200 && ReadinessResponse->GetStatusCode() <= 299))
		{
			bReady = Health == EHealth::Ok; // -V547
			ResultContext.Reset();
			ResultContext.Appendf(TEXT("Status: %s."), *ResponseString);
		}
		else
		{
			ResultContext.Reset();
			ResultContext.Appendf(TEXT("ErrorCode: %s, Error: %s, Status: %d, Response: %s."),
					*WriteToString<16>(LexToString(ReadinessResponse->GetErrorCode())),
					ReadinessResponse->GetStatusCode() == 0 ? TEXT("") : *WriteToString<64>(ReadinessResponse->GetError()),
					ReadinessResponse->GetStatusCode(),
					*ResponseString);
		}

		if (bReady || !bReadyWaitRetry)
		{
			break;
		}

		// Sleep zero to 100ms before retrying again
		const double DesiredLoopDuration = FMath::Min<double>(100.0, (double)ReadyWaitMsRemaining);
		FPlatformProcess::Sleep(static_cast<float>(FMath::Max<double>(0.0, DesiredLoopDuration - FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - LoopStartTime))/1000.0));
	}

	if (bReady)
	{
		UE_LOGF(LogDerivedDataCache, Display,
			"%ls: Using ZenServer HTTP service at %ls with namespace %ls. %ls",
			*GetName(), ZenServiceEndpoint.GetName().GetData(), *Namespace, *ResultContext);
	}
	else
	{
		if (ZenService.GetInstance().IsServiceRunningLocally())
		{
			UE_LOGF(LogDerivedDataCache, Warning,
				"%ls: Unable to reach ZenServer HTTP service at %ls with namespace %ls. %ls",
				*GetName(), ZenServiceEndpoint.GetName().GetData(), *Namespace,
				*ResultContext);
		}
		else
		{
			UE_LOGF(LogDerivedDataCache, Display,
				"%ls: Unable to reach ZenServer HTTP service at %ls with namespace %ls. %ls",
				*GetName(), ZenServiceEndpoint.GetName().GetData(), *Namespace,
				*ResultContext);
		}
	}

	return bReady;
}

void FZenCacheStore::Initialize(const FZenCacheStoreParams& Params)
{
	NodeName = Params.Name;
	LastPerformanceEvaluationTicks.store(FDateTime::UtcNow().GetTicks(), std::memory_order_relaxed);
	LastStorageSizeUpdateTicks.store(FDateTime::UtcNow().GetTicks(), std::memory_order_relaxed);
	Namespace = Params.Namespace.ToLower();
	if (Params.bNamespacedRpcEndpoint)
	{
		RpcUri = WriteToAnsiString<64>(ANSITEXTVIEW("/z$/"), Namespace, ANSITEXTVIEW("/$rpc"));
	}
	else
	{
		RpcUri = ANSITEXTVIEW("/z$/$rpc");
	}

	const uint32 MaxConnections = uint32(FMath::Clamp(FPlatformMisc::NumberOfCoresIncludingHyperthreads(), 8, 64));
	constexpr uint32 RequestPoolSize = 128;
	constexpr uint32 RequestPoolOverflowSize = 128;

	FHttpConnectionPoolParams ConnectionPoolParams;
	ConnectionPoolParams.MaxConnections = MaxConnections;
	ConnectionPoolParams.MinConnections = MaxConnections;
	ConnectionPool = IHttpManager::Get().CreateConnectionPool(ConnectionPoolParams);

	bool bReady = ReadyWait(Params);

	FHttpClientParams ClientParams;
	ClientParams.MaxRequests = RequestPoolSize + RequestPoolOverflowSize;
	ClientParams.MinRequests = RequestPoolSize;
	ClientParams.LowSpeedLimit = 1;
	ClientParams.LowSpeedTime = 25;
	ClientParams.bBypassProxy = Params.bBypassProxy;
	RequestQueue.Initialize(*ConnectionPool, ClientParams);

	bIsLocalConnection = ZenService.GetInstance().IsServiceRunningLocally() || ZenService.GetInstance().GetServiceSettings().IsAutoLaunch();
	bIsUsable = true;


	// Default to locally launched service getting the Local cache store flag.  Can be overridden by explicit value in config.
	bool bLocal = Params.bLocal.Get(bIsLocalConnection);

	// Default to non-locally launched service getting the Remote cache store flag.  Can be overridden by explicit value in config.
	// In the future this could be extended to allow the Remote flag by default (even for locally launched instances) if they have upstreams configured.
	bool bRemote = Params.bRemote.Get(!bIsLocalConnection);

	DeactivateAtMs = Params.DeactivateAtMs;

	ECacheStoreFlags Flags = ECacheStoreFlags::None;
	Flags |= Params.bWriteOnly ? ECacheStoreFlags::None : ECacheStoreFlags::Query;
	Flags |= Params.bReadOnly ? ECacheStoreFlags::None : ECacheStoreFlags::Store;
	Flags |= bLocal ? ECacheStoreFlags::Local : ECacheStoreFlags::None;
	Flags |= bRemote ? ECacheStoreFlags::Remote : ECacheStoreFlags::None;

	OperationalFlags = Flags;

	if (!bReady)
	{
		Flags = OperationalFlags & ~(ECacheStoreFlags::Store | ECacheStoreFlags::Query);
		if (bIsLocalConnection)
		{
			UE_LOGF(LogDerivedDataCache, Display,
				"%ls: Readiness check failed. "
					"It will be deactivated until responsiveness improves. "
					"If this is consistent, consider disabling this cache store through "
					"the use of the '-ddc=NoZenLocalFallback' or '-ddc=InstalledNoZenLocalFallback' "
					"commandline arguments.",
				*GetName());
		}
		else
		{
			UE_LOGF(LogDerivedDataCache, Display,
				"%ls: Readiness check failed. "
					"It will be deactivated until responsiveness improves. "
					"If this is consistent, consider disabling this cache store through "
					"environment variables or other configuration.",
				*GetName());
		}
		bDeactivatedForPerformance.store(true, std::memory_order_relaxed);
	}

	StoreOwner.Add(NodeName, this, Flags);
	TStringBuilder<256> Path(InPlace, ZenService.GetInstance().GetPath(), TEXTVIEW(" ("), Namespace, TEXTVIEW(")"));
	StoreStats = StoreOwner.CreateStats(this, Flags, TEXT("Zen"), *Params.Name, Path);
	bTryEvaluatePerformance = !GIsBuildMachine && (StoreStats != nullptr) && (DeactivateAtMs > 0.0f);

	StoreStats->SetAttribute(TEXTVIEW("Namespace"), Namespace);

	if (!bReady)
	{
		UpdateStatus();
		ActivatePerformanceEvaluationThread();
	}

	// Issue a request for stats as it will be fetched asynchronously and issuing now makes them available sooner for future callers.
	Zen::FZenCacheStats ZenStats;
	ZenService.GetInstance().GetCacheStats(ZenStats);

	MaxBatchPutKB = Params.MaxBatchPutKB;
	CacheRecordBatchSize = Params.RecordBatchSize;
	CacheChunksBatchSize = Params.ChunksBatchSize;
}

bool FZenCacheStore::IsServiceReady()
{
	return ZenService.GetInstance().IsServiceReady();
}

FCompositeBuffer FZenCacheStore::SaveRpcPackage(const FCbPackage& Package)
{
	FLargeMemoryWriter Memory;
	Zen::Http::SaveCbPackage(Package, Memory);
	uint64 PackageMemorySize = Memory.TotalSize();
	return FCompositeBuffer(FSharedBuffer::TakeOwnership(Memory.ReleaseOwnership(), PackageMemorySize, FMemory::Free));
}

void FZenCacheStore::CreateRpcRequest(IRequestOwner& Owner, FHttpRequestQueue::FOnRequest&& OnRequest)
{
	RequestQueue.CreateRequestAsync(Owner, {}, [this, OnRequest = MoveTemp(OnRequest)](THttpUniquePtr<IHttpRequest>&& Request)
	{
		if (Request)
		{
			SetRequestUri(*Request, RpcUri);
			Request->SetMethod(EHttpMethod::Post);
			Request->AddAcceptType(EHttpMediaType::CbPackage);
		}
		OnRequest(MoveTemp(Request));
	});
}

void FZenCacheStore::EnqueueAsyncRpc(IRequestOwner& Owner, FCbObject RequestObject, FOnRpcComplete&& OnComplete)
{
	CreateRpcRequest(Owner, [this, &Owner, RequestObject, OnComplete = MoveTemp(OnComplete)](THttpUniquePtr<IHttpRequest>&& Request) mutable
	{
		if (UNLIKELY(!Request))
		{
			OnComplete({}, {}, {});
			return;
		}
		Request->SetContentType(EHttpMediaType::CbObject);
		Request->SetBody(RequestObject.GetBuffer().MakeOwned());
		// Receiver will self-delete
		FRequestAsyncCbPackageReceiver* Receiver = new FRequestAsyncCbPackageReceiver(
			MoveTemp(Request),
			&Owner,
			ZenService.GetInstance(),
			MoveTemp(OnComplete));
		Receiver->SendAsync();
	}); //-V773
}

void FZenCacheStore::EnqueueAsyncRpc(IRequestOwner& Owner, const FCbPackage& RequestPackage, FOnRpcComplete&& OnComplete)
{
	CreateRpcRequest(Owner, [this, &Owner, RequestPackage, OnComplete = MoveTemp(OnComplete)](THttpUniquePtr<IHttpRequest>&& Request) mutable
	{
		if (UNLIKELY(!Request))
		{
			OnComplete({}, {}, {});
			return;
		}
		Request->SetContentType(EHttpMediaType::CbPackage);
		Request->SetBody(SaveRpcPackage(RequestPackage));
		// Receiver will self-delete
		FRequestAsyncCbPackageReceiver* Receiver = new FRequestAsyncCbPackageReceiver(
			MoveTemp(Request),
			&Owner,
			ZenService.GetInstance(),
			MoveTemp(OnComplete));
		Receiver->SendAsync();
	}); //-V773
}

void FZenCacheStore::ActivatePerformanceEvaluationThread()
{
	if (!PerformanceEvaluationThread.IsSet())
	{
		PerformanceEvaluationThread.Emplace(TEXT("ZenCacheStore Performance Evaluation"), [this]
		{
			while (!PerformanceEvaluationThreadShutdownEvent.WaitFor(FMonotonicTimeSpan::FromSeconds(30.0)))
			{
				IRequestOwner& Owner(PerformanceEvaluationRequestOwner);
				FRequestBarrier Barrier(Owner);
				RequestQueue.CreateRequestAsync(Owner, {}, [this, &Owner](THttpUniquePtr<IHttpRequest>&& Request)
				{
					if (UNLIKELY(!Request))
					{
						return;
					}
					SetRequestUri(*Request, ANSITEXTVIEW("/health/ready"));
					Request->SetMethod(EHttpMethod::Get);
					Request->AddAcceptType(EHttpMediaType::Text);
					new FAsyncHealthReceiver(MoveTemp(Request), &Owner, ZenService.GetInstance(), [this, StartTime = FMonotonicTimePoint::Now()](const THttpUniquePtr<IHttpResponse>& HttpResponse, EHealth Health)
					{
						if (Health != EHealth::Ok)
						{
							// Any non-ok health means we hold off any possibility of reactivating and we don't add the lagency to the stats
							// as the failure may be client-side and instantaneous which will create an artificially low latency measurement.
							return;
						}

						const double LatencySec = HttpResponse->GetStats().GetLatency();
						if (StoreStats)
						{
							StoreStats->AddLatency(StartTime, FMonotonicTimePoint::Now(), FMonotonicTimeSpan::FromSeconds(LatencySec));
						}
						if (!bTryEvaluatePerformance || (LatencySec * 1000 <= DeactivateAtMs))
						{
							if (PerformanceEvaluationThread.IsSet())
							{
								PerformanceEvaluationThreadShutdownEvent.Notify();
							}

							if (bDeactivatedForPerformance.load(std::memory_order_relaxed))
							{
								StoreOwner.SetFlags(this, OperationalFlags);
								UE_LOGF(LogDerivedDataCache, Display,
									"%ls: Performance has improved and meets minimum performance criteria. "
										"It will be reactivated now.",
									*GetName());
								bDeactivatedForPerformance.store(false, std::memory_order_relaxed);
								UpdateStatus();
							}
						}
					});
				});
			}
		});
	}
}

void FZenCacheStore::ConditionalUpdateStorageSize()
{
	if (!StoreStats)
	{
		return;
	}
		
	// Look for an opportunity to measure and evaluate if storage size is acceptable.
	int64 LocalStorageSizeUpdateTicks = LastStorageSizeUpdateTicks.load(std::memory_order_relaxed);
	FTimespan TimespanSinceLastStorageSizeUpdate = FDateTime::UtcNow() - FDateTime(LocalStorageSizeUpdateTicks);

	if (TimespanSinceLastStorageSizeUpdate < FTimespan::FromSeconds(30))
	{
		return;
	}

	if (!LastStorageSizeUpdateTicks.compare_exchange_strong(LocalStorageSizeUpdateTicks, FDateTime::UtcNow().GetTicks()))
	{
		return;
	}

	bool PhysicalSizeIsValid = false;
	double PhysicalSize = 0.0;
	Zen::FZenCacheStats ZenCacheStats;
	if (ZenService.GetInstance().GetCacheStats(ZenCacheStats))
	{
		PhysicalSize += ZenCacheStats.General.Size.Disk + static_cast<double>(ZenCacheStats.CID.Size.Total);
		PhysicalSizeIsValid = true;
	}
	Zen::FZenProjectStats ZenProjectStats;
	if (ZenService.GetInstance().GetProjectStats(ZenProjectStats))
	{
		PhysicalSize += ZenProjectStats.General.Size.Disk;
		PhysicalSizeIsValid = true;
	}
	if (PhysicalSizeIsValid)
	{
		StoreStats->SetTotalPhysicalSize(static_cast<uint64>(PhysicalSize));
	}
}

void FZenCacheStore::ConditionalEvaluatePerformance()
{
	if (!bTryEvaluatePerformance)
	{
		return;
	}

	// Look for an opportunity to measure and evaluate if performance is acceptable.
	int64 LocalLastPerfEvaluationTicks = LastPerformanceEvaluationTicks.load(std::memory_order_relaxed);
	FTimespan TimespanSinceLastPerfEval = FDateTime::UtcNow() - FDateTime(LocalLastPerfEvaluationTicks);

	if (TimespanSinceLastPerfEval < FTimespan::FromSeconds(30))
	{
		return;
	}

	if (!LastPerformanceEvaluationTicks.compare_exchange_strong(LocalLastPerfEvaluationTicks, FDateTime::UtcNow().GetTicks()))
	{
		return;
	}

	// We won the race and get to do the performance check

	if (PerformanceEvaluationThread.IsSet() && PerformanceEvaluationThreadShutdownEvent.IsNotified())
	{
		// Join and cleanup old thread before we consider whether we need to start a new one
		PerformanceEvaluationThread->Join();
		PerformanceEvaluationThread.Reset();
		PerformanceEvaluationThreadShutdownEvent.Reset();
	}

	uint64 DataPointCount = 0;
	FMonotonicTimeSpan MinimumLatency;
	PerformanceEvaluationTracker.MeasureAndReset(DataPointCount, MinimumLatency);
	if (DataPointCount > 20 && MinimumLatency.ToMilliseconds() > DeactivateAtMs)
	{
		if (!bDeactivatedForPerformance.load(std::memory_order_relaxed))
		{
			StoreOwner.SetFlags(this, OperationalFlags & ~(ECacheStoreFlags::Store | ECacheStoreFlags::Query));
			UE_LOGF(LogDerivedDataCache, Display,
				"%ls: Performance does not meet minimum criteria. "
					"It will be deactivated until performance measurements improve. "
					"If this is consistent, consider disabling this cache store through "
					"environment variables or other configuration.",
				*GetName());
			bDeactivatedForPerformance.store(true, std::memory_order_relaxed);
			UpdateStatus();
		}

		ActivatePerformanceEvaluationThread();
	}
}

void FZenCacheStore::UpdateStatus()
{
	if (StoreStats)
	{
		if (bDeactivatedForPerformance.load(std::memory_order_relaxed))
		{
			StoreStats->SetStatus(ECacheStoreStatusCode::Warning, NSLOCTEXT("DerivedDataCache", "DeactivatedForPerformanceOrReadiness", "Deactivated for performance or readiness"));
		}
		else
		{
			StoreStats->SetStatus(ECacheStoreStatusCode::None, {});
		}
	}
}

void FZenCacheStore::LegacyStats(FDerivedDataCacheStatsNode& OutNode)
{
	checkNoEntry();
}

void FZenCacheStore::Put(
	const TConstArrayView<FCachePutRequest> Requests,
	IRequestOwner& Owner,
	FOnCachePutComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC::PutCachedRecord);
	TRefCountPtr<FPutOp> PutOp = MakeAsyncOp<FPutOp>(*this, Owner, Requests, MoveTemp(OnComplete));
	PutOp->IssueRequests();
}

void FZenCacheStore::Get(
	const TConstArrayView<FCacheGetRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC::GetCacheRecord);
	TRefCountPtr<FGetOp> GetOp = MakeAsyncOp<FGetOp>(*this, Owner, Requests, MoveTemp(OnComplete));
	GetOp->IssueRequests();
}

void FZenCacheStore::PutValue(
	TConstArrayView<FCachePutValueRequest> Requests,
	IRequestOwner& Owner,
	FOnCachePutValueComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC::PutValue);
	TRefCountPtr<FPutValueOp> PutValueOp = MakeAsyncOp<FPutValueOp>(*this, Owner, Requests, MoveTemp(OnComplete));
	PutValueOp->IssueRequests();
}

void FZenCacheStore::GetValue(
	TConstArrayView<FCacheGetValueRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetValueComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC::GetValue);
	TRefCountPtr<FGetValueOp> GetValueOp = MakeAsyncOp<FGetValueOp>(*this, Owner, Requests, MoveTemp(OnComplete));
	GetValueOp->IssueRequests();
}

void FZenCacheStore::GetChunks(
	TConstArrayView<FCacheGetChunkRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetChunkComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC::GetChunks);
	TRefCountPtr<FGetChunksOp> GetChunksOp = MakeAsyncOp<FGetChunksOp>(*this, Owner, Requests, MoveTemp(OnComplete));
	GetChunksOp->IssueRequests();
}

void FZenCacheStoreParams::Parse(const TCHAR* NodeName, const TCHAR* Config)
{
	Name = NodeName;

	if (FString ServerId; FParse::Value(Config, TEXT("ServerID="), ServerId))
	{
		FString ServerEntry;
		const TCHAR* ServerSection = TEXT("StorageServers");
		if (GConfig->GetString(ServerSection, *ServerId, ServerEntry, GEngineIni))
		{
			Parse(NodeName, *ServerEntry);
		}
		else
		{
			UE_LOGF(LogDerivedDataCache, Warning, "%ls: Using ServerID=%ls which was not found in [%ls]", NodeName, *ServerId, ServerSection);
		}
	}

	Host = ParseConfigParamWithOverrides(NodeName, Config, TEXT("Host"), Host, /*bIsSecret*/false);

	FParse::Value(Config, TEXT("Namespace="), Namespace);
	FParse::Value(Config, TEXT("StructuredNamespace="), Namespace);

	LexFromString(bReadOnly, ParseConfigParamWithOverrides(NodeName, Config, TEXT("ReadOnly"), ::LexToString(bReadOnly), /*bIsSecret*/false));
	LexFromString(bWriteOnly, ParseConfigParamWithOverrides(NodeName, Config, TEXT("WriteOnly"), ::LexToString(bWriteOnly), /*bIsSecret*/false));

	if (bReadOnly && bWriteOnly)
	{
		UE_LOGF(LogDerivedDataCache, Display, "%ls: Both ReadOnly and WriteOnly specified.  This cache store will be inactive.", NodeName);
	}

	// Sandbox and flush configuration for use in Cold/Warm type use cases
	FParse::Value(Config, TEXT("Sandbox="), Sandbox);
	FParse::Bool(Config, TEXT("Flush="), bFlush);
	FParse::Bool(Config, TEXT("BypassProxy="), bBypassProxy);
	FParse::Bool(Config, TEXT("NamespacedRpcEndpoint="), bNamespacedRpcEndpoint);

	// Readiness waiting
	FParse::Value(Config, TEXT("ReadyWait="), ReadyWaitMs);
	FParse::Value(Config, TEXT("ReadyWaitBuildMachineMs="), ReadyWaitBuildMachineMs);
	FParse::Bool(Config, TEXT("ReadyWaitRetry="), bReadyWaitRetry);
	FParse::Bool(Config, TEXT("ReadyWaitRetryBuildMachine="), bReadyWaitRetryBuildMachine);

	// Performance deactivation
	FParse::Value(Config, TEXT("DeactivateAt="), DeactivateAtMs);

	// Explicit local and remote configuration
	if (bool bExplicitLocal = false; FParse::Bool(Config, TEXT("Local="), bExplicitLocal))
	{
		bLocal = bExplicitLocal;
	}

	if (bool bExplicitRemote = false; FParse::Bool(Config, TEXT("Remote="), bExplicitRemote))
	{
		bRemote = bExplicitRemote;
	}

	// Request batch fracturing configuration
	FParse::Value(Config, TEXT("MaxBatchPutKB="), MaxBatchPutKB);
	FParse::Value(Config, TEXT("RecordBatchSize="), RecordBatchSize);
	FParse::Value(Config, TEXT("ChunksBatchSize="), ChunksBatchSize);
}

ILegacyCacheStore* CreateZenCacheStore(const TCHAR* NodeName, const TCHAR* Config, ICacheStoreOwner& Owner)
{
	FZenCacheStoreParams Params;
	Params.Parse(NodeName, Config);

	if (Params.Host == TEXTVIEW("None"))
	{
		UE_LOGF(LogDerivedDataCache, Log, "%ls: Disabled because Host is set to 'None'", NodeName);
		return nullptr;
	}

	if (Params.Namespace.IsEmpty())
	{
		Params.Namespace = FApp::GetProjectName();
		UE_LOGF(LogDerivedDataCache, Warning, "%ls: Missing required parameter 'Namespace', falling back to '%ls'", NodeName, *Params.Namespace);
	}

	if (!Params.Host.IsEmpty())
	{
		// if a host is specified we check to see if that is potentially a list of multiple hosts and then benchmark them to find the best to use
		// if the host is empty it just means to connect to localhost, no need to do any benchmarking then
		
		TAnsiStringBuilder<256> ResolvedHost;
		double ResolvedLatency;
		FHttpHostBuilder HostBuilder;
		HostBuilder.AddFromString(Params.Host);

		if (HostBuilder.ResolveHost(/* Warning timeout */ 1.0, 4.0 /* Max duration timeout*/, ResolvedHost, ResolvedLatency))
		{
			Params.Host = FString(ResolvedHost);
		}
		else
		{
			// If we have multiple hosts and fail to resolve any, we don't want to supply an arbitrary one to use.  We should just disable this cache storage layer.
			UE_LOGF(LogDerivedDataCache, Display, "%ls: Disabled due to inability to resolve best host candidate to use, most likely none of the suggested hosts was reachable."
				" Attempted hosts were: '%ls'.", NodeName, *HostBuilder.GetHostCandidatesString());
			return nullptr;
		}
	}

	bool bHasSandbox = !Params.Sandbox.IsEmpty();
	bool bUseLocalDataCachePathOverrides = !bHasSandbox;

	FString CachePathOverride;
	if (bUseLocalDataCachePathOverrides && UE::Zen::Private::IsLocalAutoLaunched(Params.Host) && UE::Zen::Private::GetLocalDataCachePathOverride(CachePathOverride))
	{
		if (CachePathOverride == TEXT("None"))
		{
			UE_LOGF(LogDerivedDataCache, Log, "%ls: Disabled because path is set to 'None'", NodeName);
			return nullptr;
		}
	}

	TUniquePtr<FZenCacheStore> Backend;

	if (bHasSandbox)
	{
		Zen::FServiceSettings DefaultServiceSettings;
		DefaultServiceSettings.ReadFromConfig();

		if (!DefaultServiceSettings.IsAutoLaunch())
		{
			UE_LOGF(LogDerivedDataCache, Warning, "%ls: Attempting to use a sandbox when there is no default autolaunch configured to inherit settings from.  Cache will be disabled.", NodeName);
			return nullptr;
		}

		// Make a unique local instance (not the default local instance) of ZenServer
		Zen::FServiceSettings ServiceSettings;
		ServiceSettings.SettingsVariant.Emplace<Zen::FServiceAutoLaunchSettings>();
		Zen::FServiceAutoLaunchSettings& AutoLaunchSettings = ServiceSettings.SettingsVariant.Get<Zen::FServiceAutoLaunchSettings>();

		const Zen::FServiceAutoLaunchSettings& DefaultAutoLaunchSettings = DefaultServiceSettings.SettingsVariant.Get<Zen::FServiceAutoLaunchSettings>();
		AutoLaunchSettings = DefaultAutoLaunchSettings;
		// Default as one more than the default port to not collide.  Multiple sandboxes will share a desired port, but will get differing effective ports.
		AutoLaunchSettings.DesiredPort++;

		FPaths::NormalizeDirectoryName(AutoLaunchSettings.DataPath);
		AutoLaunchSettings.DataPath += TEXT("_");
		AutoLaunchSettings.DataPath += Params.Sandbox;
		AutoLaunchSettings.bIsDefaultSharedRunContext = false;

		// The unique local instances will always limit process lifetime by default for now to avoid accumulating many of them
		AutoLaunchSettings.bLimitProcessLifetime = CVarZenCacheLimitSandboxProcessLifetime.GetValueOnAnyThread();

		// Flush the cache if requested.
		uint32 MultiprocessId = UE::GetMultiprocessId();
		if (Params.bFlush && (MultiprocessId == 0))
		{
			bool bStopped = true;
			if (UE::Zen::IsLocalServiceRunning(*AutoLaunchSettings.DataPath))
			{
				bStopped = UE::Zen::StopLocalService(*AutoLaunchSettings.DataPath);
			}

			if (bStopped)
			{
				IFileManager::Get().DeleteDirectory(*(AutoLaunchSettings.DataPath / TEXT("")), /*bRequireExists*/ false, /*bTree*/ true);
			}
			else
			{
				UE_LOGF(LogDerivedDataCache, Warning, "%ls: Zen DDC could not be flushed due to an existing instance not shutting down when requested.", NodeName);
			}
		}

		Backend = MakeUnique<FZenCacheStore>(MoveTemp(ServiceSettings), Params, Owner);
	}
	else
	{
		Backend = MakeUnique<FZenCacheStore>(Params, Owner);
	}

	if (!Backend->IsUsable())
	{
		if (Backend->IsLocalConnection())
		{
			UE_LOGF(LogDerivedDataCache, Warning, "%ls: Failed to contact the service (%ls), will not use it.", NodeName, *Backend->GetName());
		}
		else
		{
			UE_LOGF(LogDerivedDataCache, Display, "%ls: Failed to contact the service (%ls), will not use it.", NodeName, *Backend->GetName());
		}
		Backend.Reset();
		return nullptr;
	}

	return Backend.Release();
}

} // namespace UE::DerivedData
