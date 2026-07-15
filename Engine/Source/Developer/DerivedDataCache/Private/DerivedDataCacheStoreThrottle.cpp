// Copyright Epic Games, Inc. All Rights Reserved.

#include "Algo/Find.h"
#include "Algo/Transform.h"
#include "DerivedDataCachePrivate.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataCacheStoreProxy.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include <atomic>

namespace UE::DerivedData
{

/**
 * A cache store that increases the latency and reduces the throughput of another cache store.
 * 1. Reproduce timings for a remote cache with a local cache, to reduce both network usage and measurement noise.
 * 2. Reproduce HDD latency and throughput even when data is stored on SSD.
 */
class FCacheStoreThrottle final : public FCacheStoreProxy
{
public:
	static FCacheStoreThrottle* TryCreate(const TCHAR* Name, const TCHAR* Config, ICacheStoreOwner& Owner)
	{
		uint32 LatencyMS = 0;
		FParse::Value(Config, TEXT("LatencyMS="), LatencyMS);
		uint32 MaxBytesPerSecond = 0;
		FParse::Value(Config, TEXT("MaxBytesPerSecond="), MaxBytesPerSecond);
		if (LatencyMS != 0 || MaxBytesPerSecond != 0)
		{
			return new FCacheStoreThrottle(Owner, LatencyMS, MaxBytesPerSecond);
		}
		return nullptr;
	}

	void Put(
		TConstArrayView<FCachePutRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutComplete&& OnComplete) final
	{
		struct FRecordSize
		{
			FCacheKey Key;
			uint64 Size;
		};
		TArray<FRecordSize, TInlineAllocator<1>> RecordSizes;
		RecordSizes.Reserve(Requests.Num());
		Algo::Transform(Requests, RecordSizes, [](const FCachePutRequest& Request) -> FRecordSize
		{
			return {Request.Record.GetKey(), Private::GetCacheRecordCompressedSize(Request.Record)};
		});

		GetInnerStore()->Put(Requests, Owner,
			[this, RecordSizes = MoveTemp(RecordSizes), State = EnterThrottlingScope(), OnComplete = MoveTemp(OnComplete)](FCachePutResponse&& Response)
			{
				const FRecordSize* Size = Algo::FindBy(RecordSizes, Response.Record.GetKey(), &FRecordSize::Key);
				CloseThrottlingScope(State, FThrottlingState(this, Size ? Size->Size : 0));
				OnComplete(MoveTemp(Response));
			});
	}

	void Get(
		TConstArrayView<FCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetComplete&& OnComplete) final
	{
		GetInnerStore()->Get(Requests, Owner,
			[this, State = EnterThrottlingScope(), OnComplete = MoveTemp(OnComplete)](FCacheGetResponse&& Response)
			{
				CloseThrottlingScope(State, FThrottlingState(this, Private::GetCacheRecordCompressedSize(Response.Record)));
				OnComplete(MoveTemp(Response));
			});
	}

	void PutValue(
		TConstArrayView<FCachePutValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutValueComplete&& OnComplete) final
	{
		struct FValueSize
		{
			FCacheKey Key;
			uint64 Size;
		};
		TArray<FValueSize, TInlineAllocator<1>> ValueSizes;
		ValueSizes.Reserve(Requests.Num());
		Algo::Transform(Requests, ValueSizes, [](const FCachePutValueRequest& Request) -> FValueSize
		{
			return {Request.Key, Request.Value.GetData().GetCompressedSize()};
		});

		GetInnerStore()->PutValue(Requests, Owner,
			[this, ValueSizes = MoveTemp(ValueSizes), State = EnterThrottlingScope(), OnComplete = MoveTemp(OnComplete)](FCachePutValueResponse&& Response)
			{
				const FValueSize* Size = Algo::FindBy(ValueSizes, Response.Key, &FValueSize::Key);
				CloseThrottlingScope(State, FThrottlingState(this, Size ? Size->Size : 0));
				OnComplete(MoveTemp(Response));
			});
	}

	void GetValue(
		TConstArrayView<FCacheGetValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetValueComplete&& OnComplete) final
	{
		GetInnerStore()->GetValue(Requests, Owner,
			[this, State = EnterThrottlingScope(), OnComplete = MoveTemp(OnComplete)](FCacheGetValueResponse&& Response)
			{
				CloseThrottlingScope(State, FThrottlingState(this, Response.Value.GetData().GetCompressedSize()));
				OnComplete(MoveTemp(Response));
			});
	}

	void GetChunks(
		TConstArrayView<FCacheGetChunkRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetChunkComplete&& OnComplete) final
	{
		GetInnerStore()->GetChunks(Requests, Owner,
			[this, State = EnterThrottlingScope(), OnComplete = MoveTemp(OnComplete)](FCacheGetChunkResponse&& Response)
			{
				CloseThrottlingScope(State, FThrottlingState(this, Response.RawData.GetSize()));
				OnComplete(MoveTemp(Response));
			});
	}

	void LegacyStats(FDerivedDataCacheStatsNode& OutNode) final
	{
		GetInnerStore()->LegacyStats(OutNode);
	}

private:
	struct FThrottlingState
	{
		double Time;
		uint64 TotalBytesTransferred;

		explicit FThrottlingState(FCacheStoreThrottle* ThrottleWrapper)
		: Time(FPlatformTime::Seconds())
		, TotalBytesTransferred(ThrottleWrapper->TotalBytesTransferred.load(std::memory_order_relaxed))
		{
		}

		explicit FThrottlingState(FCacheStoreThrottle* ThrottleWrapper, uint64 BytesTransferred)
		: Time(FPlatformTime::Seconds())
		, TotalBytesTransferred(ThrottleWrapper->TotalBytesTransferred.fetch_add(BytesTransferred, std::memory_order_relaxed) + BytesTransferred)
		{
		}
	};

	explicit FCacheStoreThrottle(ICacheStoreOwner& InOuterOwner, uint32 InLatencyMS, uint32 InMaxBytesPerSecond)
		: FCacheStoreProxy(InOuterOwner)
		, Latency(float(InLatencyMS) / 1000.0f)
		, MaxBytesPerSecond(InMaxBytesPerSecond)
	{
	}

	FThrottlingState EnterThrottlingScope()
	{
		if (Latency > 0)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ThrottlingLatency);
			FPlatformProcess::Sleep(Latency);
		}
		return FThrottlingState(this);
	}

	void CloseThrottlingScope(FThrottlingState PreviousState, FThrottlingState CurrentState)
	{
		if (MaxBytesPerSecond)
		{
			// Take into account any other transfer that might have happened during that time from any other thread so we have a global limit
			const double ExpectedTime = double(CurrentState.TotalBytesTransferred - PreviousState.TotalBytesTransferred) / MaxBytesPerSecond;
			const double ActualTime = CurrentState.Time - PreviousState.Time;
			if (ExpectedTime > ActualTime)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(ThrottlingBandwidth);
				FPlatformProcess::Sleep(float(ExpectedTime - ActualTime));
			}
		}
	}

	const float Latency;
	const uint32 MaxBytesPerSecond;
	std::atomic<uint64> TotalBytesTransferred{0};
};

ICacheStoreOwner* CreateCacheStoreThrottle(const TCHAR* Name, const TCHAR* Config, ICacheStoreOwner& Owner)
{
	return FCacheStoreThrottle::TryCreate(Name, Config, Owner);
}

} // UE::DerivedData
