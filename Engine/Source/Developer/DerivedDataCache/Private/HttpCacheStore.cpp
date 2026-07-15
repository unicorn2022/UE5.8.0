// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBackendInterface.h"
#include "DerivedDataLegacyCacheStore.h"

#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "Algo/Compare.h"
#include "Algo/NoneOf.h"
#include "Algo/Transform.h"
#include "Async/ManualResetEvent.h"
#include "Async/Mutex.h"
#include "Async/UniqueLock.h"
#include "Compression/CompressedBuffer.h"
#include "Containers/AnsiString.h"
#include "Containers/StringView.h"
#include "Containers/Ticker.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataCacheUsageStats.h"
#include "DerivedDataChunk.h"
#include "DerivedDataHttpRequestQueue.h"
#include "DerivedDataRequest.h"
#include "DerivedDataRequestOwner.h"
#include "DerivedDataSerialization.h"
#include "DerivedDataValue.h"
#include "DesktopPlatformModule.h"
#include "Dom/JsonObject.h"
#include "HAL/CriticalSection.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Http/HttpClient.h"
#include "Http/HttpHostBuilder.h"
#include "IO/IoHash.h"
#include "Logging/StructuredLog.h"
#include "Memory/SharedBuffer.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Optional.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/StringBuilder.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryPackage.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/CompactBinaryValidation.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "String/Find.h"
#include "String/LexFromString.h"

#if PLATFORM_MICROSOFT
#include "Microsoft/AllowMicrosoftPlatformTypes.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include "Microsoft/HideMicrosoftPlatformTypes.h"
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#endif

#if WITH_SSL
#include "Ssl.h"
#endif

#define UE_HTTPDDC_GETREF_REQUEST_POOL_SIZE 64
#define UE_HTTPDDC_GETBLOBS_REQUEST_POOL_SIZE 64
#define UE_HTTPDDC_EXISTS_REQUEST_POOL_SIZE 64
#define UE_HTTPDDC_PUTREF_REQUEST_POOL_SIZE 64
#define UE_HTTPDDC_PUTBLOBS_REQUEST_POOL_SIZE 64
#define UE_HTTPDDC_PUTFINALIZE_REQUEST_POOL_SIZE 64
#define UE_HTTPDDC_MAX_FAILED_LOGIN_ATTEMPTS 16
#define UE_HTTPDDC_MAX_ATTEMPTS 4

namespace UE::DerivedData
{

TRACE_DECLARE_ATOMIC_INT_COUNTER(HttpDDC_Get, TEXT("HttpDDC Get"));
TRACE_DECLARE_ATOMIC_INT_COUNTER(HttpDDC_GetHit, TEXT("HttpDDC Get Hit"));
TRACE_DECLARE_ATOMIC_INT_COUNTER(HttpDDC_Put, TEXT("HttpDDC Put"));
TRACE_DECLARE_ATOMIC_INT_COUNTER(HttpDDC_PutHit, TEXT("HttpDDC Put Hit"));
TRACE_DECLARE_ATOMIC_INT_COUNTER(HttpDDC_BytesReceived, TEXT("HttpDDC Bytes Received"));
TRACE_DECLARE_ATOMIC_INT_COUNTER(HttpDDC_BytesSent, TEXT("HttpDDC Bytes Sent"));

static TAutoConsoleVariable<bool> GDerivedDataCacheHttpUseOldBlobEndpoints(
	TEXT("DDC.Http.UseOldBlobEndpoints"),
	false,
	TEXT("Set to true to use old endpoints in CloudDDC. This will be removed in a future engine version."),
	ECVF_Default);

static FAnsiStringView GetDomainFromUri(const FAnsiStringView Uri)
{
	FAnsiStringView Domain = Uri;
	if (const int32 SchemeIndex = String::FindFirst(Domain, ANSITEXTVIEW("://")); SchemeIndex != INDEX_NONE)
	{
		Domain.RightChopInline(SchemeIndex + ANSITEXTVIEW("://").Len());
	}
	if (const int32 SlashIndex = String::FindFirstChar(Domain, '/'); SlashIndex != INDEX_NONE)
	{
		Domain.LeftInline(SlashIndex);
	}
	if (const int32 AtIndex = String::FindFirstChar(Domain, '@'); AtIndex != INDEX_NONE)
	{
		Domain.RightChopInline(AtIndex + 1);
	}
	const auto RemovePort = [](FAnsiStringView& Authority)
	{
		if (const int32 ColonIndex = String::FindLastChar(Authority, ':'); ColonIndex != INDEX_NONE)
		{
			Authority.LeftInline(ColonIndex);
		}
	};
	if (Domain.StartsWith('['))
	{
		if (const int32 LastBracketIndex = String::FindLastChar(Domain, ']'); LastBracketIndex != INDEX_NONE)
		{
			Domain.MidInline(1, LastBracketIndex - 1);
		}
		else
		{
			RemovePort(Domain);
		}
	}
	else
	{
		RemovePort(Domain);
	}
	return Domain;
}

static bool TryResolveCanonicalHost(const FAnsiStringView Uri, FAnsiStringBuilderBase& OutUri)
{
	// Append the URI until the end of the domain.
	const FAnsiStringView Domain = GetDomainFromUri(Uri);
	const int32 OutUriIndex = OutUri.Len();
	const int32 DomainIndex = int32(Domain.GetData() - Uri.GetData());
	const int32 DomainEndIndex = DomainIndex + Domain.Len();
	OutUri.Append(Uri.Left(DomainEndIndex));

	// Append the URI beyond the end of the domain before returning.
	ON_SCOPE_EXIT { OutUri.Append(Uri.RightChop(DomainEndIndex)); };

	// Try to resolve the host.
	::addrinfo* Result = nullptr;
	::addrinfo Hints{};
	Hints.ai_flags = AI_CANONNAME;
	Hints.ai_family = AF_UNSPEC;
	if (::getaddrinfo(*OutUri + OutUriIndex + DomainIndex, nullptr, &Hints, &Result) == 0)
	{
		ON_SCOPE_EXIT { ::freeaddrinfo(Result); };
		if (Result->ai_canonname)
		{
			OutUri.RemoveSuffix(Domain.Len());
			OutUri.Append(Result->ai_canonname);
			return true;
		}
	}
	return false;
}

/**
 * Encapsulation for access token shared by all requests.
 */
class FHttpAccessToken
{
public:
	void SetToken(FStringView Scheme, FStringView Token);
	inline uint32 GetSerial() const { return Serial.load(std::memory_order_relaxed); }
	friend FAnsiStringBuilderBase& operator<<(FAnsiStringBuilderBase& Builder, const FHttpAccessToken& Token);

private:
	mutable FRWLock Lock;
	TArray<ANSICHAR> Header;
	std::atomic<uint32> Serial;
};

void FHttpAccessToken::SetToken(const FStringView Scheme, const FStringView Token)
{
	FWriteScopeLock WriteLock(Lock);
	const int32 SchemeLen = FPlatformString::ConvertedLength<ANSICHAR>(Scheme.GetData(), Scheme.Len());
	const int32 TokenLen = FPlatformString::ConvertedLength<ANSICHAR>(Token.GetData(), Token.Len());

	Header.Empty(SchemeLen + 1 + TokenLen);
	
	const int32 SchemeIndex = Header.AddUninitialized(SchemeLen);
	FPlatformString::Convert(Header.GetData() + SchemeIndex, SchemeLen, Scheme.GetData(), Scheme.Len());
	
	const FAnsiStringView Seperator = ANSITEXTVIEW(" ");
	Header.Append(Seperator.GetData(), Seperator.Len());

	const int32 TokenIndex = Header.AddUninitialized(TokenLen);
	FPlatformString::Convert(Header.GetData() + TokenIndex, TokenLen, Token.GetData(), Token.Len());
	Serial.fetch_add(1, std::memory_order_relaxed);
}

FAnsiStringBuilderBase& operator<<(FAnsiStringBuilderBase& Builder, const FHttpAccessToken& Token)
{
	FReadScopeLock ReadLock(Token.Lock);
	return Builder.Append(Token.Header);
}

struct FHttpCacheStoreParams
{
	FString Name;
	FString Host;
	FString DiscoveryHost;
	FString HostPinnedPublicKeys;
	FString Namespace;
	FString HttpVersion;
	FString UnixSocketPath;
	FString OAuthProvider;
	FString OAuthClientId;
	FString OAuthSecret;
	FString OAuthScope;
	FString OAuthProviderIdentifier;
	FString OAuthAccessToken;
	FString OAuthPinnedPublicKeys;
	FString AuthScheme;

	bool bResolveHostCanonicalName = true;
	bool bReadOnly = false;
	bool bWriteOnly = false;
	bool bBypassProxy = false;

	void Parse(const TCHAR* NodeName, const TCHAR* Config);
};

//----------------------------------------------------------------------------------------------------------
// FHttpCacheStore
//----------------------------------------------------------------------------------------------------------

/**
 * Backend for a HTTP based caching service (Jupiter).
 */
class FHttpCacheStore final : public ILegacyCacheStore
{
public:
	
	/**
	 * Creates the cache store client, checks health status and attempts to acquire an access token.
	 */
	FHttpCacheStore(const FHttpCacheStoreParams& Params, ICacheStoreOwner& Owner);

	~FHttpCacheStore();

	/**
	 * Checks is cache service is usable (reachable and accessible).
	 * @return true if usable
	 */
	inline bool IsUsable() const { return bIsUsable; }

	void Put(
		TConstArrayView<FCachePutRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutComplete&& OnComplete) final;
	void Get(
		TConstArrayView<FCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetComplete&& OnComplete) final;
	void PutValue(
		TConstArrayView<FCachePutValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutValueComplete&& OnComplete) final;
	void GetValue(
		TConstArrayView<FCacheGetValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetValueComplete&& OnComplete) final;
	void GetChunks(
		TConstArrayView<FCacheGetChunkRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetChunkComplete&& OnComplete) final;

	void LegacyStats(FDerivedDataCacheStatsNode& OutNode) final;

	static FHttpCacheStore* GetAny()
	{
		return AnyInstance;
	}

	const FString& GetDomain() const { return Domain; }
	const FString& GetNamespace() const { return Namespace; }

	const FString GetAccessToken() const
	{
		TAnsiStringBuilder<128> AccessTokenBuilder;
		if (Access.IsValid())
		{
			AccessTokenBuilder << *Access;
		}
		return FString(AccessTokenBuilder);
	}

private:
	FSharedString NodeName;
	FString Domain;
	FString Namespace;
	FString OAuthProvider;
	FString OAuthClientId;
	FString OAuthSecret;
	FString OAuthScope;
	FString OAuthProviderIdentifier;
	FString OAuthAccessToken;
	FString HttpVersion;
	FString UnixSocketPath;
	FString AuthScheme;

	FAnsiString EffectiveDomain;

	ICacheStoreOwner& StoreOwner;
	ICacheStoreStats* StoreStats = nullptr;

	FDerivedDataCacheUsageStats UsageStats;
	THttpUniquePtr<IHttpConnectionPool> ConnectionPool;
	FHttpRequestQueue GetRefRequestQueue;
	FHttpRequestQueue GetBlobsRequestQueue;
	FHttpRequestQueue ExistsRequestQueue;
	FHttpRequestQueue PutRefRequestQueue;
	FHttpRequestQueue PutBlobsRequestQueue;
	FHttpRequestQueue PutFinalizeRequestQueue;

	FCriticalSection AccessCs;
	TUniquePtr<FHttpAccessToken> Access;
	FTSTicker::FDelegateHandle RefreshAccessTokenHandle;
	double RefreshAccessTokenTime = 0.0;
	uint32 LoginAttempts = 0;
	uint32 FailedLoginAttempts = 0;
	uint32 InteractiveLoginAttempts = 0;

	bool bIsUsable = false;
	bool bReadOnly = false;
	bool bWriteOnly = false;
	bool bBypassProxy = false;

	mutable FMutex OverwriteKeysMutex;
	TSet<FCacheKey> OverwriteKeys;

	static inline FHttpCacheStore* AnyInstance = nullptr;

	FHttpClientParams GetDefaultClientParams() const;

	bool AcquireAccessToken(IHttpClient* Client = nullptr);
	void SetAccessTokenAndUnlock(FScopeLock &Lock, FStringView Token, double RefreshDelay = 0.0);
	
	enum class EOperationCategory
	{
		GetRef,
		GetBlobs,
		Exists,
		PutRef,
		PutBlobs,
		PutFinalize
	};

	class FHttpOperation;

	FHttpRequestQueue& PickRequestQueue(EOperationCategory Category);

	/** Invokes the callback when an operation is available, or with null if canceled. */
	void WaitForHttpOperationAsync(IRequestOwner& Owner, EOperationCategory Category, TUniqueFunction<void (TUniquePtr<FHttpOperation>&&)>&& OnOperation);

	/** Invokes the callback when a request is available, or with null if canceled. */
	void WaitForHttpRequestAsync(IRequestOwner& Owner, EOperationCategory Category, TUniqueFunction<void (THttpUniquePtr<IHttpRequest>&&)>&& OnRequest);

	void PutCacheRecordAsync(IRequestOwner& Owner, const FCachePutRequest& Request, FOnCachePutComplete&& OnComplete);
	void PutCacheValueAsync(IRequestOwner& Owner, const FCachePutValueRequest& Request, FOnCachePutValueComplete&& OnComplete);

	void GetCacheRecordAsync(
		IRequestOwner& Owner,
		const FSharedString& Name,
		const FCacheKey& Key,
		const FCacheRecordPolicy& Policy,
		uint64 UserData,
		FOnCacheGetComplete&& OnComplete);

	void GetCacheValueAsync(
		IRequestOwner& Owner,
		FSharedString Name,
		const FCacheKey& Key,
		ECachePolicy Policy,
		ERequestOp RequestOp,
		uint64 UserData,
		FOnCacheGetValueComplete&& OnComplete);

	void FinishChunkRequest(
		const FCacheGetChunkRequest& Request,
		EStatus Status,
		const FValue& Value,
		FCompressedBufferReader& ValueReader,
		const TSharedRef<FOnCacheGetChunkComplete>& SharedOnComplete);

	void GetChunkGroupAsync(
		IRequestOwner& Owner,
		const FCacheGetChunkRequest* StartRequest,
		const FCacheGetChunkRequest* EndRequest,
		TSharedRef<FOnCacheGetChunkComplete>& SharedOnComplete);

	class FHealthCheckOp;
	class FPutPackageOp;
	class FGetRecordOp;
	class FGetValueOp;
	template <typename RequestType> class TExistsBatchOp;
	using FExistsRecordBatchOp = TExistsBatchOp<FCacheGetRequest>;
	using FExistsValueBatchOp = TExistsBatchOp<FCacheGetValueRequest>;
	using FExistsChunkBatchOp = TExistsBatchOp<FCacheGetChunkRequest>;
};

//----------------------------------------------------------------------------------------------------------
// FHttpCacheStore::FHttpOperation
//----------------------------------------------------------------------------------------------------------
class FHttpCacheStore::FHttpOperation final
{
public:
	FHttpOperation(const FHttpOperation&) = delete;
	FHttpOperation& operator=(const FHttpOperation&) = delete;

	explicit FHttpOperation(THttpUniquePtr<IHttpRequest>&& InRequest, const ICacheStoreOwner& InStoreOwner)
		: Request(MoveTemp(InRequest))
		, StoreOwner(InStoreOwner)
	{
	}

	// Prepare Request

	void SetUri(FAnsiStringView Uri) { Request->SetUri(Uri); }
	void SetUnixSocketPath(FAnsiStringView SocketPath) { Request->SetUnixSocketPath(SocketPath); }
	void SetMethod(EHttpMethod Method) { Request->SetMethod(Method); }
	void AddHeader(FAnsiStringView Name, FAnsiStringView Value) { Request->AddHeader(Name, Value); }
	void SetBody(const FCompositeBuffer& Body) { Request->SetBody(Body); }
	void SetContentType(EHttpMediaType Type) { Request->SetContentType(Type); }
	void AddAcceptType(EHttpMediaType Type) { Request->AddAcceptType(Type); }
	void SetExpectedStatusCodes(TConstArrayView<int32> Codes) { ExpectedStatusCodes = Codes; }

	// Send Request

	void Send();
	void SendAsync(IRequestOwner& Owner, TUniqueFunction<void ()>&& OnComplete);

	bool ShouldAbortForShutdown() const { return !GIsBuildMachine && StoreOwner.IsShuttingDown(); }

	// Consume Response

	int32 GetStatusCode() const { return Response->GetStatusCode(); }
	EHttpErrorCode GetErrorCode() const { return Response->GetErrorCode(); }
	EHttpMediaType GetContentType() const { return Response->GetContentType(); }
	FAnsiStringView GetHeader(FAnsiStringView Name) const { return Response->GetHeader(Name); }
	FSharedBuffer GetBody() const { return ResponseBody; }
	FString GetBodyAsString() const;
	TSharedPtr<FJsonObject> GetBodyAsJson() const;
	void GetStats(FRequestStats& OutStats) const;

	friend FStringBuilderBase& operator<<(FStringBuilderBase& Builder, const FHttpOperation& Operation)
	{
		check(Operation.Response);
		return Builder << *Operation.Response;
	}

private:
	class FHttpOperationReceiver;
	class FAsyncHttpOperationReceiver;

	FSharedBuffer ResponseBody;
	THttpUniquePtr<IHttpRequest> Request;
	THttpUniquePtr<IHttpResponse> Response;
	TArray<int32, TInlineAllocator<4>> ExpectedStatusCodes;
	uint32 AttemptCount = 0;
	const ICacheStoreOwner& StoreOwner;
};

class FHttpCacheStore::FHttpOperation::FHttpOperationReceiver final : public IHttpReceiver
{
public:
	FHttpOperationReceiver(const FHttpOperationReceiver&) = delete;
	FHttpOperationReceiver& operator=(const FHttpOperationReceiver&) = delete;

	explicit FHttpOperationReceiver(FHttpOperation* InOperation, IHttpReceiver* InNext = nullptr)
		: Operation(InOperation)
		, Next(InNext)
		, BodyReceiver(BodyArray, this)
	{
	}

	FHttpOperation* GetOperation() const { return Operation; }

private:
	IHttpReceiver* OnCreate(IHttpResponse& LocalResponse) final
	{
		++Operation->AttemptCount;
		return &BodyReceiver;
	}

	IHttpReceiver* OnComplete(IHttpResponse& LocalResponse) final
	{
		Operation->ResponseBody = MakeSharedBufferFromArray(MoveTemp(BodyArray));

		LogResponse(LocalResponse);

		if (!ShouldRetry(LocalResponse))
		{
			Operation->Request.Reset();
		}

		return Next;
	}

	bool ShouldRetry(IHttpResponse& LocalResponse) const
	{
		if (Operation->AttemptCount >= UE_HTTPDDC_MAX_ATTEMPTS || Operation->ShouldAbortForShutdown())
		{
			return false;
		}

		EHttpErrorCode ErrorCode = LocalResponse.GetErrorCode();
		if ((ErrorCode == EHttpErrorCode::TimedOut) || (ErrorCode == EHttpErrorCode::Unknown))
		{
			return true;
		}

		// Make a new attempt if the response status code is any of:
		// 429 - Too many requests
		int32 StatusCode = LocalResponse.GetStatusCode();
		if (StatusCode == 429)
		{
			return true;
		}

		return false;
	}

	void LogResponse(IHttpResponse& LocalResponse) const
	{
		if (UE_LOG_ACTIVE(LogDerivedDataCache, Display))
		{
			EHttpErrorCode ErrorCode = LocalResponse.GetErrorCode();
			const int32 StatusCode = LocalResponse.GetStatusCode();
			bool bUnexpectedError = false;
			if (ErrorCode == EHttpErrorCode::None)
			{
				bUnexpectedError = !((StatusCode >= 200 && StatusCode < 300) || Operation->ExpectedStatusCodes.Contains(StatusCode));
			}
			else if (ErrorCode == EHttpErrorCode::Canceled)
			{
				// No logging, this is expected to happen.
			}
			else
			{
				bUnexpectedError = true;
			}

			TStringBuilder<80> StatsText;
			if (bUnexpectedError || UE_LOG_ACTIVE(LogDerivedDataCache, Verbose))
			{
				const FHttpResponseStats& Stats = LocalResponse.GetStats();
				if (Stats.SendSize)
				{
					StatsText << TEXTVIEW("sent ") << Stats.SendSize << TEXTVIEW(" bytes, ");
				}
				if (Stats.RecvSize)
				{
					StatsText << TEXTVIEW("received ") << Stats.RecvSize << TEXTVIEW(" bytes, ");
				}
				StatsText.Appendf(TEXT("%.3f seconds %.3f|%.3f|%.3f|%.3f"), Stats.TotalTime, Stats.NameResolveTime, Stats.ConnectTime, Stats.TlsConnectTime, Stats.StartTransferTime);
			}

			if (bUnexpectedError)
			{
				FString Body = Operation->GetBodyAsString();
				Body.ReplaceCharInline(TEXT('\r'), TEXT(' '));
				Body.ReplaceCharInline(TEXT('\n'), TEXT(' '));
				UE_LOGF(LogDerivedDataCache, Display,
					"HTTP: %ls (%ls) %ls", *WriteToString<256>(LocalResponse), *StatsText, *Body);
			}
			else
			{
				UE_LOGF(LogDerivedDataCache, Verbose, "HTTP: %ls (%ls)", *WriteToString<256>(LocalResponse), *StatsText);
			}
		}
	}

private:
	FHttpOperation* Operation;
	IHttpReceiver* Next;
	TArray64<uint8> BodyArray;
	FHttpByteArrayReceiver BodyReceiver{BodyArray, this};
};

class FHttpCacheStore::FHttpOperation::FAsyncHttpOperationReceiver final : public FRequestBase, public IHttpReceiver
{
public:
	FAsyncHttpOperationReceiver(const FAsyncHttpOperationReceiver&) = delete;
	FAsyncHttpOperationReceiver& operator=(const FAsyncHttpOperationReceiver&) = delete;

	FAsyncHttpOperationReceiver(FHttpOperation* InOperation, IRequestOwner* InOwner, TUniqueFunction<void ()>&& InOperationComplete)
		: Owner(InOwner)
		, BaseReceiver(InOperation, this)
		, OperationComplete(MoveTemp(InOperationComplete))
	{}

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
		return &BaseReceiver;
	}

	IHttpReceiver* OnComplete(IHttpResponse& LocalResponse) final
	{
		Owner->End(this, [Self = this]
		{
			FHttpOperation* Operation = Self->BaseReceiver.GetOperation();
			if (IHttpRequest* LocalRequest = Operation->Request.Get())
			{
				// Retry as indicated by the request not being reset.
				TRefCountPtr<FAsyncHttpOperationReceiver> Receiver = new FAsyncHttpOperationReceiver(Operation, Self->Owner, MoveTemp(Self->OperationComplete));
				LocalRequest->SendAsync(Receiver, Operation->Response);
			}
			else if (Self->OperationComplete)
			{
				// Launch a task for the completion function since it can execute arbitrary code.
				Self->Owner->LaunchTask(TEXT("HttpOperationComplete"), [Self = TRefCountPtr(Self)]
				{
					Self->OperationComplete();
				});
			}
		});
		return nullptr;
	}

private:
	TRefCountPtr<IHttpResponseMonitor> CopyMonitorRef()
	{
		TUniqueLock Lock(MonitorMutex);
		return Monitor;
	}

	IRequestOwner* Owner;
	FHttpOperationReceiver BaseReceiver;
	TUniqueFunction<void ()> OperationComplete;
	TRefCountPtr<IHttpResponseMonitor> Monitor;
	FMutex MonitorMutex;
};

void FHttpCacheStore::FHttpOperation::Send()
{
	FHttpOperationReceiver Receiver(this);
	do
	{
		Request->Send(&Receiver, Response);
	}
	while (Request);
}

void FHttpCacheStore::FHttpOperation::SendAsync(IRequestOwner& Owner, TUniqueFunction<void ()>&& OnComplete)
{
	TRefCountPtr<FAsyncHttpOperationReceiver> Receiver = new FAsyncHttpOperationReceiver(this, &Owner, MoveTemp(OnComplete));
	Request->SendAsync(Receiver, Response);
}

FString FHttpCacheStore::FHttpOperation::GetBodyAsString() const
{
	static_assert(sizeof(uint8) == sizeof(UTF8CHAR));
	uint64 ResponseBodySize = ResponseBody.GetSize();
	EHttpMediaType ContentType = GetContentType();
	switch (ContentType)
	{
	case EHttpMediaType::Text:
	case EHttpMediaType::Json:
	case EHttpMediaType::Yaml:
		return FString::ConstructFromPtrSize((const UTF8CHAR*)ResponseBody.GetData(), int32(FMath::Clamp<uint64>(ResponseBodySize, 0, MAX_int32)));
	case EHttpMediaType::CbObject:
		if (ValidateCompactBinary(ResponseBody, ECbValidateMode::Default) == ECbValidateError::None)
		{
			TUtf8StringBuilder<1024> JsonStringBuilder;
			const FCbObject ResponseObject(ResponseBody);
			CompactBinaryToCompactJson(ResponseObject, JsonStringBuilder);
			return FString(JsonStringBuilder);
		}
		return FString::Printf(TEXT("Invalid compact binary object of size %" UINT64_FMT), ResponseBodySize);
	case EHttpMediaType::CompressedBinary:
		{
			FCompressedBuffer Buffer = FCompressedBuffer::FromCompressed(ResponseBody);
			if (!Buffer.IsNull())
			{
				return FString::Printf(TEXT("CompressedBuffer rawhash:%s, rawsize:%" UINT64_FMT ", compressedsize:%" UINT64_FMT), *WriteToString<32>(Buffer.GetRawHash()), Buffer.GetRawSize(), Buffer.GetCompressedSize());
			}
			return FString::Printf(TEXT("Invalid compressed buffer of size %" UINT64_FMT), ResponseBodySize);
		}
	default:
		return FString::Printf(TEXT("Content type '%s' of size %" UINT64_FMT), *WriteToString<32>(LexToString(ContentType)), ResponseBodySize);
	}
}

TSharedPtr<FJsonObject> FHttpCacheStore::FHttpOperation::GetBodyAsJson() const
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(GetBodyAsString());
	FJsonSerializer::Deserialize(JsonReader, JsonObject);
	return JsonObject;
}

void FHttpCacheStore::FHttpOperation::GetStats(FRequestStats& OutStats) const
{
	const FHttpResponseStats& Stats = Response->GetStats();
	TUniqueLock Lock(OutStats.Mutex.Get());
	OutStats.PhysicalReadSize += Stats.RecvSize;
	OutStats.PhysicalWriteSize += Stats.SendSize;
	if (const EHttpMethod Method = Response->GetMethod(); Method == EHttpMethod::Get || Method == EHttpMethod::Head)
	{
		OutStats.AddLatency(FMonotonicTimeSpan::FromSeconds(Stats.GetLatency()));
	}
}

//----------------------------------------------------------------------------------------------------------
// FHttpCacheStore::FHealthCheckOp
//----------------------------------------------------------------------------------------------------------
class FHttpCacheStore::FHealthCheckOp final
{
public:
	FHealthCheckOp(FHttpCacheStore& CacheStore, IHttpClient& Client, const ICacheStoreOwner& StoreOwner)
		: Operation(Client.TryCreateRequest({}), StoreOwner)
		, Owner(EPriority::High)
		, NodeName(*CacheStore.NodeName)
	{
		Operation.SetUri(WriteToAnsiString<256>(CacheStore.EffectiveDomain, ANSITEXTVIEW("/health/ready")));
		Operation.SetUnixSocketPath(StringCast<ANSICHAR>(*CacheStore.UnixSocketPath));
		Operation.SendAsync(Owner, []{});
	}

	bool IsReady()
	{
		Owner.Wait();
		const FString Body = Operation.GetBodyAsString();
		if (Operation.GetStatusCode() == 200)
		{
			UE_LOGF(LogDerivedDataCache, Display, "%ls: HTTP DDC: %ls", NodeName, *Body);
			return true;
		}
		else
		{
			UE_LOGF(LogDerivedDataCache, Warning, "%ls: Unable to reach HTTP DDC at %ls. %ls",
				NodeName, *WriteToString<256>(Operation), *Body);
			return false;
		}
	}

private:
	FHttpOperation Operation;
	FRequestOwner Owner;
	const TCHAR* NodeName;
};

//----------------------------------------------------------------------------------------------------------
// FHttpCacheStore::FPutPackageOp
//----------------------------------------------------------------------------------------------------------
class FHttpCacheStore::FPutPackageOp final : public FRefCountedObject
{
public:
	struct FResponse
	{
		EStatus Status = EStatus::Error;
		FCbObject ExistingObject;
	};
	using FOnPackageComplete = TUniqueFunction<void (FResponse&& Response)>;

	static TRefCountPtr<FPutPackageOp> New(FHttpCacheStore& CacheStore, IRequestOwner& Owner, const FSharedString& Name)
	{
		return new FPutPackageOp(CacheStore, Owner, Name);
	}

	void Put(const FCacheKey& Key, const FCacheRecordPolicy& Policy, FCbPackage&& Package, FOnPackageComplete&& OnComplete);

	IRequestOwner& GetOwner() const { return Owner; }

	const FRequestStats& ReadStats() const { return RequestStats; }
	FRequestStats& EditStats() { return RequestStats; }

private:
	FHttpCacheStore& CacheStore;
	IRequestOwner& Owner;
	const FSharedString Name;

	FCacheKey Key;
	FCbObject Object;
	FIoHash ObjectHash;
	FOnPackageComplete OnPackageComplete;

	FCbObject ExistingObject;

	FRequestStats RequestStats;

	std::atomic<uint32> SuccessfulBlobUploads = 0;
	std::atomic<uint32> PendingBlobUploads = 0;
	uint32 TotalBlobUploads = 0;

	struct FCachePutRefResponse
	{
		TConstArrayView<FIoHash> NeededBlobHashes;
		EStatus Status = EStatus::Error;
	};
	using FOnCachePutRefComplete = TUniqueFunction<void(FCachePutRefResponse&& Response)>;

	FPutPackageOp(FHttpCacheStore& CacheStore, IRequestOwner& Owner, const FSharedString& Name);

	void BeginOperation(bool bFinalize, bool bAllowOverwrite, FOnCachePutRefComplete&& OnComplete);

	void BeginPutRef(TUniquePtr<FHttpOperation> Operation, bool bFinalize, bool bAllowOverwrite, FOnCachePutRefComplete&& OnComplete);
	void EndPutRef(TUniquePtr<FHttpOperation> Operation, bool bFinalize, FOnCachePutRefComplete&& OnComplete);

	void BeginPutBlobs(FCbPackage&& Package, FCachePutRefResponse&& Response);
	void EndPutBlob(FHttpOperation* Operation, uint64 LogicalSize);

	void EndPutRefFinalize(FCachePutRefResponse&& Response);

	void EndPut(EStatus Status);
};

FHttpCacheStore::FPutPackageOp::FPutPackageOp(FHttpCacheStore& InCacheStore, IRequestOwner& InOwner, const FSharedString& InName)
	: CacheStore(InCacheStore)
	, Owner(InOwner)
	, Name(InName)
{
	RequestStats.Name = Name;
}

void FHttpCacheStore::FPutPackageOp::Put(const FCacheKey& InKey, const FCacheRecordPolicy& Policy, FCbPackage&& Package, FOnPackageComplete&& OnComplete)
{
	const bool bAllowOverwrite = !EnumHasAnyFlags(Policy.GetRecordPolicy(), ECachePolicy::QueryRemote);
	Key = InKey;
	Object = Package.GetObject();
	ObjectHash = Package.GetObjectHash();
	OnPackageComplete = MoveTemp(OnComplete);
	BeginOperation(/*bFinalize*/ false, bAllowOverwrite, [Self = TRefCountPtr(this), Package = MoveTemp(Package)](FCachePutRefResponse&& Response) mutable
	{
		Self->BeginPutBlobs(MoveTemp(Package), MoveTemp(Response));
	});
}

void FHttpCacheStore::FPutPackageOp::BeginOperation(bool bFinalize, bool bAllowOverwrite, FOnCachePutRefComplete&& OnComplete)
{
	CacheStore.WaitForHttpOperationAsync(Owner, bFinalize ? EOperationCategory::PutFinalize : EOperationCategory::PutRef,
	[Self = TRefCountPtr(this), bFinalize, bAllowOverwrite, OnComplete = MoveTemp(OnComplete)](TUniquePtr<FHttpOperation>&& Operation) mutable
	{
		Self->BeginPutRef(MoveTemp(Operation), bFinalize, bAllowOverwrite, MoveTemp(OnComplete));
	});
}

void FHttpCacheStore::FPutPackageOp::BeginPutRef(
	TUniquePtr<FHttpOperation> Operation,
	bool bFinalize,
	bool bAllowOverwrite,
	FOnCachePutRefComplete&& OnComplete)
{
	if (UNLIKELY(!Operation))
	{
		OnComplete({{}, EStatus::Canceled});
		return;
	}

	FRequestTimer RequestTimer(RequestStats);

	TAnsiStringBuilder<64> Bucket;
	Algo::Transform(Key.Bucket.ToString(), AppendChars(Bucket), FCharAnsi::ToLower);

	TAnsiStringBuilder<256> RefsUri;
	RefsUri << CacheStore.EffectiveDomain << ANSITEXTVIEW("/api/v1/refs/") << CacheStore.Namespace << '/' << Bucket << '/' << Key.Hash;
	if (bFinalize)
	{
		RefsUri << ANSITEXTVIEW("/finalize/") << ObjectHash;
	}

	FHttpOperation& LocalOperation = *Operation;
	LocalOperation.SetUri(RefsUri);
	LocalOperation.SetUnixSocketPath(StringCast<ANSICHAR>(*CacheStore.UnixSocketPath));
	if (bFinalize)
	{
		LocalOperation.SetMethod(EHttpMethod::Post);
		LocalOperation.SetContentType(EHttpMediaType::FormUrlEncoded);
	}
	else
	{
		LocalOperation.SetMethod(EHttpMethod::Put);
		LocalOperation.SetContentType(EHttpMediaType::CbObject);
		LocalOperation.AddHeader(ANSITEXTVIEW("X-Jupiter-IoHash"), WriteToAnsiString<48>(ObjectHash));
		LocalOperation.SetBody(Object.GetBuffer());

		// Only specify the X-Jupiter-Allow-Overwrite header if we are specifically allowing overwrites.
		// If we are not allowing overwrites, we leave the header unspecified.  This is both to avoid
		// sending excess bytes with every put, but also to allow the server to apply its own default
		// behavior instead of having the client specify the overwrite behavior explicitly.
		if (bAllowOverwrite)
		{
			LocalOperation.AddHeader(ANSITEXTVIEW("X-Jupiter-Allow-Overwrite"), WriteToAnsiString<8>(bAllowOverwrite));
		}
		else
		{
			LocalOperation.SetExpectedStatusCodes({409});
		}
	}
	LocalOperation.AddAcceptType(EHttpMediaType::Json);

	RequestTimer.Stop();
	LocalOperation.SendAsync(Owner, [Self = TRefCountPtr(this), Operation = MoveTemp(Operation), bFinalize, OnComplete = MoveTemp(OnComplete)]() mutable
	{
		Operation->GetStats(Self->RequestStats);
		Self->EndPutRef(MoveTemp(Operation), bFinalize, MoveTemp(OnComplete));
	});
}

void FHttpCacheStore::FPutPackageOp::EndPutRef(
	TUniquePtr<FHttpOperation> Operation,
	bool bFinalize,
	FOnCachePutRefComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_PutPackage_EndPutRef);

	if (const EHttpErrorCode ErrorCode = Operation->GetErrorCode(); ErrorCode != EHttpErrorCode::None)
	{
		if (ErrorCode != EHttpErrorCode::Canceled)
		{
			UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache put failed due to error or retry exhaustion on record for %ls from '%ls'",
				*CacheStore.NodeName, *WriteToString<96>(Key), *Name);
		}
		OnComplete({{}, ErrorCode == EHttpErrorCode::Canceled ? EStatus::Canceled : EStatus::Error});
		return;
	}

	if (const int32 StatusCode = Operation->GetStatusCode(); StatusCode < 200 || StatusCode > 204)
	{
		if (StatusCode == 409 && Operation->GetContentType() == EHttpMediaType::CbObject &&
			ValidateCompactBinary(Operation->GetBody(), ECbValidateMode::Default) == ECbValidateError::None)
		{
			ExistingObject = FCbObject(Operation->GetBody());
		}
		OnComplete({{}, StatusCode == 409 ? EStatus::Ok : EStatus::Error});
		return;
	}

	FRequestTimer RequestTimer(RequestStats);

	TArray<FIoHash> NeededBlobHashes;

	// Useful when debugging issues related to compressed/uncompressed blobs being returned from Jupiter
	static const bool bHttpCacheAlwaysPut = FParse::Param(FCommandLine::Get(), TEXT("HttpCacheAlwaysPut"));

	if (bHttpCacheAlwaysPut && !bFinalize)
	{
		Object.IterateAttachments([&NeededBlobHashes](FCbFieldView AttachmentFieldView)
		{
			FIoHash AttachmentHash = AttachmentFieldView.AsHash();
			if (!AttachmentHash.IsZero())
			{
				NeededBlobHashes.Add(AttachmentHash);
			}
		});
	}
	else if (TSharedPtr<FJsonObject> ResponseObject = Operation->GetBodyAsJson())
	{
		TArray<FString> NeedsArrayStrings;
		ResponseObject->TryGetStringArrayField(TEXT("needs"), NeedsArrayStrings);

		NeededBlobHashes.Reserve(NeedsArrayStrings.Num());
		for (const FString& NeededString : NeedsArrayStrings)
		{
			FIoHash BlobHash;
			LexFromString(BlobHash, *NeededString);
			if (!BlobHash.IsZero())
			{
				NeededBlobHashes.Add(BlobHash);
			}
		}
	}

	RequestTimer.Stop();
	OnComplete({NeededBlobHashes, EStatus::Ok});
}

void FHttpCacheStore::FPutPackageOp::BeginPutBlobs(FCbPackage&& Package, FCachePutRefResponse&& Response)
{
	if (Response.Status != EStatus::Ok)
	{
		if (Response.Status == EStatus::Error)
		{
			UE_LOGF(LogDerivedDataCache, Display, "%ls: Failed to put reference object for put of %ls from '%ls'",
				*CacheStore.NodeName, *WriteToString<96>(Key), *Name);
		}
		EndPut(Response.Status);
		return;
	}

	FRequestTimer RequestTimer(RequestStats);

	// TODO: blob uploading and finalization should be replaced with a single batch compressed blob upload endpoint in the future.
	TStringBuilder<128> ExpectedHashes;
	bool bExpectedHashesSerialized = false;

	// Needed blob upload (if any missing)
	TArray<FCompressedBuffer> Blobs;
	for (const FIoHash& NeededBlobHash : Response.NeededBlobHashes)
	{
		if (const FCbAttachment* Attachment = Package.FindAttachment(NeededBlobHash))
		{
			FCompressedBuffer Blob;
			if (Attachment->IsCompressedBinary())
			{
				Blob = Attachment->AsCompressedBinary();
			}
			else if (Attachment->IsBinary())
			{
				Blob = FValue::Compress(Attachment->AsCompositeBinary()).GetData();
			}
			else
			{
				Blob = FValue::Compress(Attachment->AsObject().GetBuffer()).GetData();
			}
			Blobs.Emplace(MoveTemp(Blob));
		}
		else
		{
			if (!bExpectedHashesSerialized)
			{
				for (const FCbAttachment& PackageAttachment : Package.GetAttachments())
				{
					ExpectedHashes << PackageAttachment.GetHash() << TEXTVIEW(", ");
				}
				if (ExpectedHashes.Len() >= 2)
				{
					ExpectedHashes.RemoveSuffix(2);
				}
				bExpectedHashesSerialized = true;
			}
			UE_LOGF(LogDerivedDataCache, Display, "%ls: Server reported needed hash '%ls' that is outside the set of expected hashes (%ls) for put of %ls from '%ls'",
				*CacheStore.NodeName, *WriteToString<96>(NeededBlobHash), *ExpectedHashes, *WriteToString<96>(Key), *Name);
		}
	}

	if (Blobs.IsEmpty())
	{
		RequestTimer.Stop();
		EndPut(EStatus::Ok);
		return;
	}

	TotalBlobUploads = Blobs.Num();
	PendingBlobUploads.store(TotalBlobUploads, std::memory_order_relaxed);

	FRequestBarrier Barrier(Owner);
	for (const FCompressedBuffer& Blob : Blobs)
	{
		CacheStore.WaitForHttpOperationAsync(Owner, EOperationCategory::PutBlobs, [Self = TRefCountPtr(this), Blob](TUniquePtr<FHttpOperation>&& Operation)
		{
			if (UNLIKELY(!Operation))
			{
				Self->EndPutBlob(nullptr, 0);
				return;
			}

			FHttpOperation& LocalOperation = *Operation;
			if (GDerivedDataCacheHttpUseOldBlobEndpoints.GetValueOnAnyThread())
			{
				LocalOperation.SetUri(WriteToAnsiString<256>(Self->CacheStore.EffectiveDomain, ANSITEXTVIEW("/api/v1/compressed-blobs/"), Self->CacheStore.Namespace, '/', Blob.GetRawHash()));
			}
			else 
			{
				LocalOperation.SetUri(WriteToAnsiString<256>(Self->CacheStore.EffectiveDomain, ANSITEXTVIEW("/api/v1/refs/"), Self->CacheStore.Namespace, '/', Self->Key.Bucket, '/', Self->Key.Hash, ANSITEXTVIEW("/blobs/"), Blob.GetRawHash()));
			}
			LocalOperation.SetUnixSocketPath(StringCast<ANSICHAR>(*Self->CacheStore.UnixSocketPath));
			LocalOperation.SetMethod(EHttpMethod::Put);
			LocalOperation.SetContentType(EHttpMediaType::CompressedBinary);
			LocalOperation.SetBody(Blob.GetCompressed());
			LocalOperation.SendAsync(Self->Owner, [Self, Operation = MoveTemp(Operation), LogicalSize = Blob.GetRawSize()]
			{
				Operation->GetStats(Self->RequestStats);
				Self->EndPutBlob(Operation.Get(), LogicalSize);
			});
		});
	}
}

void FHttpCacheStore::FPutPackageOp::EndPutBlob(FHttpOperation* Operation, uint64 LogicalSize)
{
	if (Operation)
	{
		const int32 StatusCode = Operation->GetStatusCode();
		if (Operation->GetErrorCode() == EHttpErrorCode::None && StatusCode >= 200 && StatusCode <= 204)
		{
			SuccessfulBlobUploads.fetch_add(1, std::memory_order_relaxed);
			TUniqueLock Lock(RequestStats.Mutex.Get());
			RequestStats.LogicalWriteSize += LogicalSize;
		}
	}

	if (PendingBlobUploads.fetch_sub(1, std::memory_order_relaxed) == 1)
	{
		const uint32 LocalSuccessfulBlobUploads = SuccessfulBlobUploads.load(std::memory_order_relaxed);
		if (Owner.IsCanceled())
		{
			EndPut(EStatus::Canceled);
		}
		else if (LocalSuccessfulBlobUploads == TotalBlobUploads)
		{
			BeginOperation(/*bFinalize*/ true, /*bAllowOverwrite*/ false, [Self = TRefCountPtr(this)](FCachePutRefResponse&& Response)
			{
				Self->EndPutRefFinalize(MoveTemp(Response));
			});
		}
		else
		{
			const uint32 FailedBlobUploads = TotalBlobUploads - LocalSuccessfulBlobUploads;
			UE_LOGF(LogDerivedDataCache, Log, "%ls: Failed to put %d/%d blobs for put of %ls from '%ls'",
				*CacheStore.NodeName, FailedBlobUploads, TotalBlobUploads, *WriteToString<96>(Key), *Name);
			EndPut(EStatus::Error);
		}
	}
}

void FHttpCacheStore::FPutPackageOp::EndPutRefFinalize(FCachePutRefResponse&& Response)
{
	if (Response.Status == EStatus::Error)
	{
		UE_LOGF(LogDerivedDataCache, Log, "%ls: Failed to finalize reference object for put of %ls from '%ls'",
			*CacheStore.NodeName, *WriteToString<96>(Key), *Name);
	}

	EndPut(Response.Status);
}

void FHttpCacheStore::FPutPackageOp::EndPut(EStatus Status)
{
	RequestStats.EndTime = FMonotonicTimePoint::Now();
	RequestStats.Status = Status;
	// Ensuring that the OnPackageComplete method is destroyed by the time we exit this method by moving it to a local scope variable
	FOnPackageComplete LocalOnComplete = MoveTemp(OnPackageComplete);
	LocalOnComplete({Status, MoveTemp(ExistingObject)});
	if (CacheStore.StoreStats)
	{
		CacheStore.StoreStats->AddRequest(RequestStats);
	}
}

//----------------------------------------------------------------------------------------------------------
// FHttpCacheStore::FGetRecordOp
//----------------------------------------------------------------------------------------------------------
class FHttpCacheStore::FGetRecordOp final : public FRefCountedObject
{
public:
	static TRefCountPtr<FGetRecordOp> New(FHttpCacheStore& CacheStore, IRequestOwner& Owner, const FSharedString& Name)
	{
		return new FGetRecordOp(CacheStore, Owner, Name);
	}

	struct FRecordResponse
	{
		FCacheRecord Record;
		EStatus Status = EStatus::Error;
	};
	using FOnRecordComplete = TUniqueFunction<void (FRecordResponse&& Response)>;

	void GetRecordOnly(const FCacheKey& Key, const ECachePolicy RecordPolicy, FOnRecordComplete&& OnComplete);
	void GetRecord(const FCacheKey& Key, const FCacheRecordPolicy& Policy, FOnRecordComplete&& OnComplete);

	struct FValueResponse
	{
		FValueWithId Value;
		EStatus Status = EStatus::Error;
	};
	using FOnValueComplete = TUniqueFunction<void (FValueResponse&& Response)>;

	void GetValues(TConstArrayView<FValueWithId> Values, FOnValueComplete&& OnComplete);

	const FRequestStats& ReadStats() const { return RequestStats; }
	FRequestStats& EditStats() { return RequestStats; }
	void RecordStats(EStatus Status);

	int32 GetFailedValues() const { return FailedValues; }
	void PrepareForPendingValues(int32 InPendingValues) { PendingValues = InPendingValues; }
	bool FinishPendingValueFetch(const FValueWithId& Value, bool bAppendToPackage);
	bool FinishPendingValueExists(EStatus Status);

private:
	FGetRecordOp(FHttpCacheStore& CacheStore, IRequestOwner& Owner, const FSharedString& Name);

	void EndGetRef(TUniquePtr<FHttpOperation> Operation);

	void BeginGetValues(const FCacheRecord& Record, const FCacheRecordPolicy& Policy, FOnRecordComplete&& OnComplete);
	void EndGetValues(const FCacheRecordPolicy& Policy, EStatus Status);

	void BeginGetValue(TUniquePtr<FHttpOperation>&& Operation, const FValueWithId& Value, const TSharedRef<FOnValueComplete>& OnComplete);
	void EndGetValue(FHttpOperation& Operation, const FValueWithId& Value, const FOnValueComplete& OnComplete);

	FHttpCacheStore& CacheStore;
	IRequestOwner& Owner;
	FSharedString Name;
	FCacheKey Key;
	FCbPackage Package;
	FOnRecordComplete OnRecordComplete;

	FRequestStats RequestStats;

	int32 PendingValues = 0;
	int32 FailedValues = 0;
	mutable FMutex Mutex;
};

FHttpCacheStore::FGetRecordOp::FGetRecordOp(FHttpCacheStore& InCacheStore, IRequestOwner& InOwner, const FSharedString& InName)
	: CacheStore(InCacheStore)
	, Owner(InOwner)
	, Name(InName)
{
	RequestStats.Name = Name;
}

void FHttpCacheStore::FGetRecordOp::GetRecordOnly(const FCacheKey& InKey, const ECachePolicy RecordPolicy, FOnRecordComplete&& InOnComplete)
{
	FRequestTimer RequestTimer(RequestStats);

	Key = InKey;

	if (!CacheStore.IsUsable())
	{
		UE_LOGF(LogDerivedDataCache, VeryVerbose,
			"%ls: Skipped get of %ls from '%ls' because this cache store is not available",
			*CacheStore.NodeName, *WriteToString<96>(Key), *Name);
		return InOnComplete({FCacheRecord(Key), EStatus::Error});
	}

	// Skip the request if querying the cache is disabled.
	if (!EnumHasAnyFlags(RecordPolicy, ECachePolicy::QueryRemote))
	{
		UE_LOGF(LogDerivedDataCache, VeryVerbose, "%ls: Skipped get of %ls from '%ls' due to cache policy",
			*CacheStore.NodeName, *WriteToString<96>(Key), *Name);
		return InOnComplete({FCacheRecord(Key), EStatus::Error});
	}

	OnRecordComplete = MoveTemp(InOnComplete);
	RequestStats.Bucket = Key.Bucket;

	RequestTimer.Stop();
	CacheStore.WaitForHttpOperationAsync(Owner, EOperationCategory::GetRef, [Self = TRefCountPtr(this)](TUniquePtr<FHttpOperation>&& Operation)
	{
		if (UNLIKELY(!Operation))
		{
			Self->EndGetRef(MoveTemp(Operation));
			return;
		}

		TAnsiStringBuilder<64> Bucket;
		Algo::Transform(Self->Key.Bucket.ToString(), AppendChars(Bucket), FCharAnsi::ToLower);

		FHttpOperation& LocalOperation = *Operation;
		LocalOperation.SetUri(WriteToAnsiString<256>(Self->CacheStore.EffectiveDomain, ANSITEXTVIEW("/api/v1/refs/"), Self->CacheStore.Namespace, '/', Bucket, '/', Self->Key.Hash));
		LocalOperation.SetUnixSocketPath(StringCast<ANSICHAR>(*Self->CacheStore.UnixSocketPath));
		LocalOperation.SetMethod(EHttpMethod::Get);
		LocalOperation.AddAcceptType(EHttpMediaType::CbObject);
		if (TUniqueLock Lock(Self->CacheStore.OverwriteKeysMutex); Self->CacheStore.OverwriteKeys.Contains(Self->Key))
		{
			LocalOperation.AddHeader(ANSITEXTVIEW("X-Jupiter-Force-Consistent-Read"), ANSITEXTVIEW("true"));
		}
		LocalOperation.SetExpectedStatusCodes({404});

		LocalOperation.SendAsync(Self->Owner, [Self, Operation = MoveTemp(Operation)]() mutable
		{
			Operation->GetStats(Self->RequestStats);
			Self->EndGetRef(MoveTemp(Operation));
		});
	});
}

void FHttpCacheStore::FGetRecordOp::EndGetRef(TUniquePtr<FHttpOperation> Operation)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_GetPackage_EndGetRef);

	FRequestTimer RequestTimer(RequestStats);

	FOptionalCacheRecord Record;
	EStatus Status = Operation ? EStatus::Error : EStatus::Canceled;
	ON_SCOPE_EXIT
	{
		Operation.Reset();
		if (Record.IsNull())
		{
			Record = FCacheRecord(Key);
		}
		RequestTimer.Stop();
		// Ensuring that the OnRecordComplete method is destroyed by the time we exit this method by moving it to a local scope variable
		FOnRecordComplete LocalOnComplete = MoveTemp(OnRecordComplete);
		LocalOnComplete({MoveTemp(Record).Get(), Status});
	};

	if (UNLIKELY(!Operation))
	{
		return;
	}

	if (const EHttpErrorCode ErrorCode = Operation->GetErrorCode(); ErrorCode != EHttpErrorCode::None)
	{
		if (ErrorCode != EHttpErrorCode::Canceled)
		{
			UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache miss due to error or retry exhaustion on record for %ls from '%ls'",
				*CacheStore.NodeName, *WriteToString<96>(Key), *Name);
		}
		Status = ErrorCode == EHttpErrorCode::Canceled ? EStatus::Canceled : EStatus::Error;
		return;
	}

	const int32 StatusCode = Operation->GetStatusCode();
	if (StatusCode < 200 || StatusCode > 204)
	{
		UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache miss with missing package for %ls from '%ls'",
			*CacheStore.NodeName, *WriteToString<96>(Key), *Name);
		return;
	}

	FSharedBuffer Body = Operation->GetBody();

	if (FAnsiStringView ReceivedHashStr = Operation->GetHeader("X-Jupiter-IoHash"); !ReceivedHashStr.IsEmpty())
	{
		FIoHash ReceivedHash(ReceivedHashStr);
		FIoHash ComputedHash = FIoHash::HashBuffer(Body.GetView());
		if (ReceivedHash != ComputedHash)
		{
			UE_LOGF(LogDerivedDataCache, Display,
				"%ls: Cache miss with corrupted record received hash %ls when expected hash %ls for %ls from '%ls'",
				*CacheStore.NodeName, *WriteToString<48>(ComputedHash),
				*WriteToString<48>(ReceivedHash), *WriteToString<96>(Key), *Name);
			return;
		}
	}

	if (ValidateCompactBinary(Body, ECbValidateMode::Default) != ECbValidateError::None)
	{
		UE_LOGF(LogDerivedDataCache, Log, "%ls: Cache miss with invalid package for %ls from '%ls'",
			*CacheStore.NodeName, *WriteToString<96>(Key), *Name);
		return;
	}

	Package = FCbPackage(FCbObject(Body));
	Record = FCacheRecord::Load(Package);

	if (Record.IsNull())
	{
		UE_LOGF(LogDerivedDataCache, Log, "%ls: Cache miss with record load failure for %ls from '%ls'",
			*CacheStore.NodeName, *WriteToString<96>(Key), *Name);
		return;
	}

	Status = EStatus::Ok;
}

void FHttpCacheStore::FGetRecordOp::GetRecord(const FCacheKey& LocalKey, const FCacheRecordPolicy& Policy, FOnRecordComplete&& OnComplete)
{
	GetRecordOnly(LocalKey, Policy.GetRecordPolicy(), [Self = TRefCountPtr(this), Policy, OnComplete = MoveTemp(OnComplete)](FRecordResponse&& Response) mutable
	{
		if (Response.Status == EStatus::Ok)
		{
			Self->BeginGetValues(Response.Record, Policy, MoveTemp(OnComplete));
		}
		else
		{
			OnComplete(MoveTemp(Response));
		}
	});
}

bool FHttpCacheStore::FGetRecordOp::FinishPendingValueFetch(const FValueWithId& Value, bool bAppendToPackage)
{
	TDynamicUniqueLock Lock(Mutex);
	const bool bComplete = --PendingValues == 0;
	if (Value.HasData())
	{
		if (bAppendToPackage)
		{
			Package.AddAttachment(FCbAttachment(Value.GetData()));
		}
	}
	else
	{
		++FailedValues;
	}
	return bComplete;
}

bool FHttpCacheStore::FGetRecordOp::FinishPendingValueExists(EStatus Status)
{
	TDynamicUniqueLock Lock(Mutex);
	const bool bComplete = --PendingValues == 0;
	if (Status != EStatus::Ok)
	{
		++FailedValues;
	}
	return bComplete;
}

void FHttpCacheStore::FGetRecordOp::BeginGetValues(const FCacheRecord& Record, const FCacheRecordPolicy& Policy, FOnRecordComplete&& OnComplete)
{
	FRequestTimer RequestTimer(RequestStats);

	OnRecordComplete = MoveTemp(OnComplete);

	TArray<FValueWithId> RequiredGets;

	for (const FValueWithId& Value : Record.GetValues())
	{
		const ECachePolicy ValuePolicy = Policy.GetValuePolicy(Value.GetId());
		if (EnumHasAnyFlags(ValuePolicy, ECachePolicy::QueryRemote) && !EnumHasAnyFlags(ValuePolicy, ECachePolicy::SkipData))
		{
			RequiredGets.Emplace(Value);
		}
	}

	PrepareForPendingValues(RequiredGets.Num());

	RequestTimer.Stop();

	if (PendingValues == 0)
	{
		EndGetValues(Policy, EStatus::Ok);
		return;
	}

	GetValues(RequiredGets, [Self = TRefCountPtr(this), Policy](FValueResponse&& Response)
	{
		if (Self->FinishPendingValueFetch(Response.Value, true))
		{
			Self->EndGetValues(Policy, Response.Status);
		}
	});
}

void FHttpCacheStore::FGetRecordOp::EndGetValues(const FCacheRecordPolicy& Policy, EStatus Status)
{
	FCacheRecordBuilder RecordBuilder(Key);
	if (FOptionalCacheRecord Record = FCacheRecord::Load(Package))
	{
		if (!EnumHasAnyFlags(Policy.GetRecordPolicy(), ECachePolicy::SkipMeta))
		{
			RecordBuilder.SetMeta(CopyTemp(Record.Get().GetMeta()));
		}
		for (const FValueWithId& Value : Record.Get().GetValues())
		{
			const ECachePolicy ValuePolicy = Policy.GetValuePolicy(Value.GetId());
			if (EnumHasAnyFlags(ValuePolicy, ECachePolicy::QueryRemote) && !EnumHasAnyFlags(ValuePolicy, ECachePolicy::SkipData))
			{
				if (Status == EStatus::Ok && !Value.HasData())
				{
					Status = EStatus::Error;
				}
				RecordBuilder.AddValue(Value);
			}
			else
			{
				RecordBuilder.AddValue(Value.RemoveData());
			}
		}
	}

	if (FailedValues)
	{
		Status = EStatus::Error;
	}

	// Ensuring that the OnRecordComplete method is destroyed by the time we exit this method by moving it to a local scope variable
	FOnRecordComplete LocalOnComplete = MoveTemp(OnRecordComplete);
	LocalOnComplete({RecordBuilder.Build(), Status});
}

void FHttpCacheStore::FGetRecordOp::GetValues(TConstArrayView<FValueWithId> Values, FOnValueComplete&& OnComplete)
{
	int32 MissingDataCount = 0;
	for (const FValueWithId& Value : Values)
	{
		if (Value.HasData())
		{
			OnComplete({Value, EStatus::Ok});
			continue;
		}
		++MissingDataCount;
	}

	if (MissingDataCount == 0)
	{
		return;
	}

	// TODO: Jupiter does not currently provide a batched GET. Once it does, fetch every blob in one request.

	FRequestTimer RequestTimer(RequestStats);
	RequestTimer.Stop();

	FRequestBarrier Barrier(Owner);
	TSharedRef<FOnValueComplete> SharedOnComplete = MakeShared<FOnValueComplete>(MoveTemp(OnComplete));
	for (const FValueWithId& Value : Values)
	{
		if (Value.HasData())
		{
			(*SharedOnComplete)({Value, EStatus::Ok});
			continue;
		}

		CacheStore.WaitForHttpOperationAsync(Owner, EOperationCategory::GetBlobs, [Self = TRefCountPtr(this), SharedOnComplete, Value](TUniquePtr<FHttpOperation>&& Operation)
		{
			Self->BeginGetValue(MoveTemp(Operation), Value, SharedOnComplete);
		});
	}
}

void FHttpCacheStore::FGetRecordOp::BeginGetValue(
	TUniquePtr<FHttpOperation>&& Operation,
	const FValueWithId& Value,
	const TSharedRef<FOnValueComplete>& OnComplete)
{
	if (UNLIKELY(!Operation))
	{
		(*OnComplete)({Value, EStatus::Canceled});
		return;
	}

	FHttpOperation& LocalOperation = *Operation;
	if (GDerivedDataCacheHttpUseOldBlobEndpoints.GetValueOnAnyThread())
	{
		LocalOperation.SetUri(WriteToAnsiString<256>(CacheStore.EffectiveDomain, ANSITEXTVIEW("/api/v1/compressed-blobs/"), CacheStore.Namespace, '/', Value.GetRawHash()));
	}
	else 
	{
		LocalOperation.SetUri(WriteToAnsiString<256>(CacheStore.EffectiveDomain, ANSITEXTVIEW("/api/v1/refs/"), CacheStore.Namespace, '/', Key.Bucket, '/', Key.Hash, ANSITEXTVIEW("/blobs/"), Value.GetRawHash()));
	}
	LocalOperation.SetUnixSocketPath(StringCast<ANSICHAR>(*CacheStore.UnixSocketPath));
	LocalOperation.SetMethod(EHttpMethod::Get);
	LocalOperation.AddAcceptType(EHttpMediaType::Any);
	LocalOperation.SetExpectedStatusCodes({404});
	LocalOperation.SendAsync(Owner, [Self = TRefCountPtr(this), Operation = MoveTemp(Operation), OnComplete, Value]
	{
		Self->EndGetValue(*Operation, Value, *OnComplete);
	});
}

void FHttpCacheStore::FGetRecordOp::EndGetValue(FHttpOperation& Operation, const FValueWithId& Value, const FOnValueComplete& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_GetPackage_GetValues_OnResponse);

	FRequestTimer RequestTimer(RequestStats);
	Operation.GetStats(RequestStats);

	bool bHit = false;
	FCompressedBuffer CompressedBuffer;
	if (Operation.GetErrorCode() == EHttpErrorCode::None && Operation.GetStatusCode() == 200)
	{
		switch (Operation.GetContentType())
		{
		case EHttpMediaType::Any:
		case EHttpMediaType::CompressedBinary:
			CompressedBuffer = FCompressedBuffer::FromCompressed(Operation.GetBody());
			bHit = true;
			break;
		case EHttpMediaType::Binary:
			CompressedBuffer = FValue::Compress(Operation.GetBody()).GetData();
			bHit = true;
			break;
		default:
			break;
		}

		TUniqueLock Lock(RequestStats.Mutex.Get());
		RequestStats.LogicalReadSize += CompressedBuffer.GetRawSize();
	}

	RequestTimer.Stop();

	if (bHit)
	{
		if (FAnsiStringView ReceivedHashStr = Operation.GetHeader("X-Jupiter-IoHash"); !ReceivedHashStr.IsEmpty())
		{
			FIoHash ReceivedHash(ReceivedHashStr);
			FIoHash ComputedHash = FIoHash::HashBuffer(Operation.GetBody().GetView());
			if (ReceivedHash != ComputedHash)
			{
				UE_LOGF(LogDerivedDataCache, Display,
					"%ls: Cache miss with corrupted value %ls received hash %ls when expected hash %ls for %ls from '%ls'",
					*CacheStore.NodeName, *WriteToString<32>(Value.GetId()), *WriteToString<48>(ComputedHash),
					*WriteToString<48>(ReceivedHash), *WriteToString<96>(Key), *Name);
				OnComplete({ Value, EStatus::Error });
				return;
			}
		}

		if (CompressedBuffer.GetRawHash() == Value.GetRawHash() && CompressedBuffer.GetRawSize() == Value.GetRawSize())
		{
			OnComplete({FValueWithId(Value.GetId(), MoveTemp(CompressedBuffer)), EStatus::Ok});
		}
		else
		{
			UE_LOGF(LogDerivedDataCache, Display,
				"%ls: Cache miss with corrupted value %ls with hash %ls for %ls from '%ls'",
				*CacheStore.NodeName, *WriteToString<32>(Value.GetId()), *WriteToString<48>(Value.GetRawHash()),
				*WriteToString<96>(Key), *Name);
			OnComplete({Value, EStatus::Error});
		}
	}
	else if (Operation.GetErrorCode() == EHttpErrorCode::Canceled)
	{
		OnComplete({Value, EStatus::Canceled});
	}
	else
	{
		if (Operation.GetErrorCode() != EHttpErrorCode::None)
		{
			UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache miss due to error or retry exhaustion on value %ls for %ls from '%ls'",
				*CacheStore.NodeName, *WriteToString<32>(Value.GetId()), *WriteToString<96>(Key), *Name);
		}
		else
		{
			UE_LOGF(LogDerivedDataCache, Verbose,
				"%ls: Cache miss with missing value %ls with hash %ls for %ls from '%ls'",
				*CacheStore.NodeName, *WriteToString<32>(Value.GetId()), *WriteToString<48>(Value.GetRawHash()),
				*WriteToString<96>(Key), *Name);
		}
		OnComplete({Value, EStatus::Error});
	}
}

void FHttpCacheStore::FGetRecordOp::RecordStats(EStatus Status)
{
	RequestStats.EndTime = FMonotonicTimePoint::Now();
	RequestStats.Status = Status;
	if (CacheStore.StoreStats)
	{
		CacheStore.StoreStats->AddRequest(RequestStats);
	}
}

//----------------------------------------------------------------------------------------------------------
// FHttpCacheStore::FGetValueOp
//----------------------------------------------------------------------------------------------------------
class FHttpCacheStore::FGetValueOp final : public FRefCountedObject
{
public:
	struct FResponse
	{
		const FSharedString& Name;
		const FCacheKey& Key;
		FValue Value;
		EStatus Status = EStatus::Error;
	};
	using FOnComplete = TUniqueFunction<void (FResponse&& Response)>;

	static TRefCountPtr<FGetValueOp> New(FHttpCacheStore& CacheStore, IRequestOwner& Owner, const FSharedString& Name)
	{
		return new FGetValueOp(CacheStore, Owner, Name);
	}

	void Get(const FCacheKey& Key, ECachePolicy Policy, FOnComplete&& OnComplete);

	const FRequestStats& ReadStats() const { return RequestStats; }
	FRequestStats& EditStats() { return RequestStats; }

private:
	FGetValueOp(FHttpCacheStore& CacheStore, IRequestOwner& Owner, const FSharedString& Name);

	void BeginGetRef(TUniquePtr<FHttpOperation>&& Operation);
	void EndGetRef(FHttpOperation& Operation);
	void EndGet(FResponse&& Response);

	FHttpCacheStore& CacheStore;
	IRequestOwner& Owner;
	FSharedString Name;
	FCacheKey Key;
	ECachePolicy Policy = ECachePolicy::None;
	FOnComplete OnComplete;
	FRequestStats RequestStats;
};

FHttpCacheStore::FGetValueOp::FGetValueOp(FHttpCacheStore& InCacheStore, IRequestOwner& InOwner, const FSharedString& InName)
	: CacheStore(InCacheStore)
	, Owner(InOwner)
	, Name(InName)
{
	RequestStats.Name = Name;
}

void FHttpCacheStore::FGetValueOp::Get(const FCacheKey& InKey, ECachePolicy InPolicy, FOnComplete&& InOnComplete)
{
	FRequestTimer RequestTimer(RequestStats);

	Key = InKey;
	Policy = InPolicy;
	OnComplete = MoveTemp(InOnComplete);

	RequestTimer.Stop();
	CacheStore.WaitForHttpOperationAsync(Owner, EOperationCategory::GetRef, [Self = TRefCountPtr(this)](TUniquePtr<FHttpOperation>&& Operation)
	{
		Self->BeginGetRef(MoveTemp(Operation));
	});
}

void FHttpCacheStore::FGetValueOp::BeginGetRef(TUniquePtr<FHttpOperation>&& Operation)
{
	if (UNLIKELY(!Operation))
	{
		UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache miss with failed with canceled request for %ls from '%ls'",
			*CacheStore.NodeName, *WriteToString<96>(Key), *Name);
		EndGet({Name, Key, {}, EStatus::Canceled});
		return;
	}

	FRequestTimer RequestTimer(RequestStats);

	const bool bSkipData = EnumHasAnyFlags(Policy, ECachePolicy::SkipData);

	TAnsiStringBuilder<64> Bucket;
	Algo::Transform(Key.Bucket.ToString(), AppendChars(Bucket), FCharAnsi::ToLower);

	FHttpOperation& LocalOperation = *Operation;
	LocalOperation.SetUri(WriteToAnsiString<256>(CacheStore.EffectiveDomain, ANSITEXTVIEW("/api/v1/refs/"), CacheStore.Namespace, '/', Bucket, '/', Key.Hash));
	LocalOperation.SetUnixSocketPath(StringCast<ANSICHAR>(*CacheStore.UnixSocketPath));
	LocalOperation.SetMethod(EHttpMethod::Get);
	if (bSkipData)
	{
		LocalOperation.AddAcceptType(EHttpMediaType::CbObject);
	}
	else
	{
		LocalOperation.AddHeader(ANSITEXTVIEW("Accept"), ANSITEXTVIEW("application/x-jupiter-inline"));
	}
	if (TUniqueLock Lock(CacheStore.OverwriteKeysMutex); CacheStore.OverwriteKeys.Contains(Key))
	{
		LocalOperation.AddHeader(ANSITEXTVIEW("X-Jupiter-Force-Consistent-Read"), ANSITEXTVIEW("true"));
	}
	LocalOperation.SetExpectedStatusCodes({404});

	RequestTimer.Stop();
	LocalOperation.SendAsync(Owner, [Self = TRefCountPtr(this), Operation = MoveTemp(Operation)]() mutable
	{
		Self->EndGetRef(*Operation);
	});
}

void FHttpCacheStore::FGetValueOp::EndGetRef(FHttpOperation& Operation)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_GetValue_EndGetRef);

	Operation.GetStats(RequestStats);

	if (const EHttpErrorCode ErrorCode = Operation.GetErrorCode(); ErrorCode != EHttpErrorCode::None)
	{
		if (ErrorCode != EHttpErrorCode::Canceled)
		{
			UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache miss due to error or retry exhaustion for %ls from '%ls'",
				*CacheStore.NodeName, *WriteToString<96>(Key), *Name);
		}
		return EndGet({Name, Key, {}, ErrorCode == EHttpErrorCode::Canceled ? EStatus::Canceled : EStatus::Error});
	}

	const int32 StatusCode = Operation.GetStatusCode();
	if (StatusCode < 200 || StatusCode > 204)
	{
		UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache miss with failed HTTP request for %ls from '%ls'",
			*CacheStore.NodeName, *WriteToString<96>(Key), *Name);
		return EndGet({Name, Key, {}, EStatus::Error});
	}

	FSharedBuffer Body = Operation.GetBody();

	if (EnumHasAnyFlags(Policy, ECachePolicy::SkipData))
	{
		if (FAnsiStringView ReceivedHashStr = Operation.GetHeader("X-Jupiter-IoHash"); !ReceivedHashStr.IsEmpty())
		{
			FRequestTimer RequestTimer(RequestStats);
			FIoHash ReceivedHash(ReceivedHashStr);
			FIoHash ComputedHash = FIoHash::HashBuffer(Body.GetView());
			if (ReceivedHash != ComputedHash)
			{
				RequestTimer.Stop();
				UE_LOGF(LogDerivedDataCache, Display,
					"%ls: Cache miss with corrupted value reference received hash %ls when expected hash %ls for %ls from '%ls'",
					*CacheStore.NodeName, *WriteToString<48>(ComputedHash),
					*WriteToString<48>(ReceivedHash), *WriteToString<96>(Key), *Name);
				return EndGet({ Name, Key, {}, EStatus::Error });
			}
		}

		if (ValidateCompactBinary(Body, ECbValidateMode::Default) != ECbValidateError::None)
		{
			UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache miss with invalid package for %ls from '%ls'",
				*CacheStore.NodeName, *WriteToString<96>(Key), *Name);
			return EndGet({Name, Key, {}, EStatus::Error});
		}

		const FCbObjectView Object = FCbObject(Body);
		const FIoHash RawHash = Object["RawHash"].AsHash();
		const uint64 RawSize = Object["RawSize"].AsUInt64(MAX_uint64);
		if (RawHash.IsZero() || RawSize == MAX_uint64)
		{
			UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache miss with invalid value for %ls from '%ls'",
				*CacheStore.NodeName, *WriteToString<96>(Key), *Name);
			return EndGet({Name, Key, {}, EStatus::Error});
		}

		EndGet({Name, Key, FValue(RawHash, RawSize), EStatus::Ok});
	}
	else
	{
		FCompressedBuffer CompressedBuffer = FCompressedBuffer::FromCompressed(Body);

		if (FAnsiStringView ReceivedHashStr = Operation.GetHeader("X-Jupiter-InlinePayloadHash"); !ReceivedHashStr.IsEmpty())
		{
			FRequestTimer RequestTimer(RequestStats);
			FIoHash ReceivedHash(ReceivedHashStr);
			FIoHash ComputedHash = FIoHash::HashBuffer(Body.GetView());
			if (ReceivedHash != ComputedHash)
			{
				RequestTimer.Stop();
				UE_LOGF(LogDerivedDataCache, Display,
					"%ls: Cache miss with corrupted value received hash %ls when expected hash %ls for %ls from '%ls'",
					*CacheStore.NodeName, *WriteToString<48>(ComputedHash),
					*WriteToString<48>(ReceivedHash), *WriteToString<96>(Key), *Name);
				return EndGet({Name, Key, {}, EStatus::Error});
			}
		}

		if (!CompressedBuffer)
		{
			CompressedBuffer = FCompressedBuffer::Compress(Body);
		}

		if (!CompressedBuffer)
		{
			UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache miss with invalid package for %ls from '%ls'",
				*CacheStore.NodeName, *WriteToString<96>(Key), *Name);
			return EndGet({Name, Key, {}, EStatus::Error});
		}

		EndGet({Name, Key, FValue(CompressedBuffer), EStatus::Ok});
	}
}

void FHttpCacheStore::FGetValueOp::EndGet(FResponse&& Response)
{
	RequestStats.LogicalReadSize += Response.Value.GetRawSize();
	RequestStats.EndTime = FMonotonicTimePoint::Now();
	RequestStats.Status = Response.Status;
	// Ensuring that the OnComplete method is destroyed by the time we exit this method by moving it to a local scope variable
	FOnComplete LocalOnComplete = MoveTemp(OnComplete);
	LocalOnComplete(MoveTemp(Response));
	if (CacheStore.StoreStats)
	{
		CacheStore.StoreStats->AddRequest(RequestStats);
	}
}

//----------------------------------------------------------------------------------------------------------
// FHttpCacheStore::TExistsBatchOp
//----------------------------------------------------------------------------------------------------------
template <typename RequestType>
class FHttpCacheStore::TExistsBatchOp final : public FRefCountedObject
{
public:
	using FOnComplete = TCacheOnCompleteFor<RequestType>;

	static TRefCountPtr<TExistsBatchOp> New(FHttpCacheStore& CacheStore, IRequestOwner& Owner)
	{
		return new TExistsBatchOp(CacheStore, Owner);
	}

	void Exists(TConstArrayView<RequestType> Requests, FOnComplete&& OnComplete);

private:
	TExistsBatchOp(FHttpCacheStore& CacheStore, IRequestOwner& Owner);

	void BeginExists(TUniquePtr<FHttpOperation>&& Operation, FCbFieldIterator&& Body);
	void EndExists(FHttpOperation& Operation);

	void EndRequest(const RequestType& Request, FCbObjectView ResponseView, EStatus Status);

	FHttpCacheStore& CacheStore;
	IRequestOwner& Owner;
	TArray<RequestType> Requests;
	FOnComplete OnComplete;
	FRequestStats RequestStats;
};

template <typename RequestType>
FHttpCacheStore::TExistsBatchOp<RequestType>::TExistsBatchOp(FHttpCacheStore& InCacheStore, IRequestOwner& InOwner)
	: CacheStore(InCacheStore)
	, Owner(InOwner)
{
}

static bool PolicyHasAnyFlags(ECachePolicy Policy, ECachePolicy Contains)
{
	return EnumHasAnyFlags(Policy, Contains);
}

static bool PolicyHasAnyFlags(const FCacheRecordPolicy& Policy, ECachePolicy Contains)
{
	return EnumHasAnyFlags(Policy.GetRecordPolicy(), Contains);
}

template <typename RequestType>
void FHttpCacheStore::TExistsBatchOp<RequestType>::Exists(TConstArrayView<RequestType> InRequests, FOnComplete&& InOnComplete)
{
	FRequestTimer RequestTimer(RequestStats);

	OnComplete = MoveTemp(InOnComplete);

	Requests.Empty(InRequests.Num());
	for (const RequestType& Request : InRequests)
	{
		if (!CacheStore.IsUsable())
		{
			UE_LOGF(LogDerivedDataCache, VeryVerbose,
				"%ls: Skipped exists check of %ls from '%ls' because this cache store is not available",
				*CacheStore.NodeName, *WriteToString<96>(Request.Key), *Request.Name);
			OnComplete(Request.MakeResponse(EStatus::Error));
			continue;
		}

		if (!PolicyHasAnyFlags(Request.Policy, ECachePolicy::QueryRemote))
		{
			UE_LOGF(LogDerivedDataCache, VeryVerbose, "%ls: Skipped exists check of %ls from '%ls' due to cache policy",
				*CacheStore.NodeName, *WriteToString<96>(Request.Key), *Request.Name);
			OnComplete(Request.MakeResponse(EStatus::Error));
			continue;
		}

		Requests.Emplace(Request);
	}

	if (Requests.IsEmpty())
	{
		return;
	}

	FCbWriter BodyWriter;
	BodyWriter.BeginObject();
	BodyWriter.BeginArray(ANSITEXTVIEW("ops"));
	uint32 OpIndex = 0;
	for (const RequestType& Request : Requests)
	{
		BodyWriter.BeginObject();
		BodyWriter.AddInteger(ANSITEXTVIEW("opId"), OpIndex);
		BodyWriter.AddString(ANSITEXTVIEW("op"), ANSITEXTVIEW("GET"));
		const FCacheKey& Key = Request.Key;
		TAnsiStringBuilder<64> Bucket;
		Algo::Transform(Key.Bucket.ToString(), AppendChars(Bucket), FCharAnsi::ToLower);
		BodyWriter.AddString(ANSITEXTVIEW("bucket"), Bucket);
		BodyWriter.AddString(ANSITEXTVIEW("key"), LexToString(Key.Hash));
		BodyWriter.AddBool(ANSITEXTVIEW("resolveAttachments"), true);
		BodyWriter.EndObject();
		++OpIndex;
	}
	BodyWriter.EndArray();
	BodyWriter.EndObject();
	FCbFieldIterator Body = BodyWriter.Save();

	RequestTimer.Stop();
	CacheStore.WaitForHttpOperationAsync(Owner, EOperationCategory::Exists, [Self = TRefCountPtr(this), Body = MoveTemp(Body)](TUniquePtr<FHttpOperation>&& Operation) mutable
	{
		Self->BeginExists(MoveTemp(Operation), MoveTemp(Body));
	});
}

template <typename RequestType>
void FHttpCacheStore::TExistsBatchOp<RequestType>::BeginExists(TUniquePtr<FHttpOperation>&& Operation, FCbFieldIterator&& Body)
{
	if (UNLIKELY(!Operation))
	{
		for (const RequestType& Request : Requests)
		{
			UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache miss with canceled request for %ls from '%ls'",
				*CacheStore.NodeName, *WriteToString<96>(Request.Key), *Request.Name);
			RequestStats.Bucket = Request.Key.Bucket;
			EndRequest(Request, {}, EStatus::Canceled);
		}
		return;
	}

	FRequestTimer RequestTimer(RequestStats);

	FHttpOperation& LocalOperation = *Operation;
	LocalOperation.SetUri(WriteToAnsiString<256>(CacheStore.EffectiveDomain, ANSITEXTVIEW("/api/v1/refs/"), CacheStore.Namespace));
	LocalOperation.SetUnixSocketPath(StringCast<ANSICHAR>(*CacheStore.UnixSocketPath));
	LocalOperation.SetMethod(EHttpMethod::Post);
	LocalOperation.SetContentType(EHttpMediaType::CbObject);
	LocalOperation.AddAcceptType(EHttpMediaType::CbObject);
	LocalOperation.SetBody(FCompositeBuffer(Body.GetOuterBuffer()));
	const auto HasOverwriteKey = [this](const RequestType& Request)
	{
		TUniqueLock Lock(CacheStore.OverwriteKeysMutex);
		return CacheStore.OverwriteKeys.Contains(Request.Key);
	};
	if (Algo::AnyOf(Requests, HasOverwriteKey))
	{
		LocalOperation.AddHeader(ANSITEXTVIEW("X-Jupiter-Force-Consistent-Read"), ANSITEXTVIEW("true"));
	}

	RequestTimer.Stop();
	LocalOperation.SendAsync(Owner, [Self = TRefCountPtr(this), Operation = MoveTemp(Operation)]() mutable
	{
		Self->EndExists(*Operation);
	});
}

template <typename RequestType>
void FHttpCacheStore::TExistsBatchOp<RequestType>::EndExists(FHttpOperation& Operation)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_ExistsBatch_EndExists);
	ON_SCOPE_EXIT
	{
		// OnComplete may be called multiple times in the span of EndExists, but by the time this method finishes, it will never be used and can be destroyed
		OnComplete.Reset();
	};

	FRequestTimer RequestTimer(RequestStats);

	Operation.GetStats(RequestStats);

	// Divide the stats evenly among the requests.
	RequestStats.PhysicalReadSize /= Requests.Num();
	RequestStats.PhysicalWriteSize /= Requests.Num();
	RequestStats.MainThreadTime = FMonotonicTimeSpan::FromSeconds(RequestStats.MainThreadTime.ToSeconds() / Requests.Num());
	RequestStats.OtherThreadTime = FMonotonicTimeSpan::FromSeconds(RequestStats.OtherThreadTime.ToSeconds() / Requests.Num());
	RequestStats.EndTime = FMonotonicTimePoint::Now();
	RequestStats.Op = ERequestOp::Get;

	if (const EHttpErrorCode ErrorCode = Operation.GetErrorCode(); ErrorCode != EHttpErrorCode::None)
	{
		RequestTimer.Stop();
		for (const RequestType& Request : Requests)
		{
			if (ErrorCode != EHttpErrorCode::Canceled)
			{
				UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache miss with failed HTTP request due to error or retry exhaustion on record for %ls from '%ls'",
					*CacheStore.NodeName, *WriteToString<96>(Request.Key), *Request.Name);
			}
			RequestStats.Bucket = Request.Key.Bucket;
			EndRequest(Request, {}, EStatus::Error);
		}
		return;
	}

	const int32 OverallStatusCode = Operation.GetStatusCode();
	if (OverallStatusCode < 200 || OverallStatusCode > 204)
	{
		RequestTimer.Stop();
		for (const RequestType& Request : Requests)
		{
			UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache miss with failed non-success status code for %ls from '%ls'",
				*CacheStore.NodeName, *WriteToString<96>(Request.Key), *Request.Name);
			RequestStats.Bucket = Request.Key.Bucket;
			EndRequest(Request, {}, EStatus::Error);
		}
		return;
	}

	FMemoryView ResponseView = Operation.GetBody();
	if (ValidateCompactBinary(ResponseView, ECbValidateMode::Default) != ECbValidateError::None)
	{
		RequestTimer.Stop();
		for (const RequestType& Request : Requests)
		{
			UE_LOGF(LogDerivedDataCache, Log, "%ls: Cache miss with corrupt response for %ls from '%ls'.",
				*CacheStore.NodeName, *WriteToString<96>(Request.Key), *Request.Name);
			RequestStats.Bucket = Request.Key.Bucket;
			EndRequest(Request, {}, EStatus::Error);
		}
		return;
	}

	RequestTimer.Stop();

	const FCbObjectView ResponseObject(ResponseView.GetData());
	const FCbArrayView Results = ResponseObject[ANSITEXTVIEW("results")].AsArrayView();

	if (Results.Num() != Requests.Num())
	{
		UE_LOG(LogDerivedDataCache, Log,
			TEXT("%s: Cache exists returned unexpected quantity of results (expected %d, got %" UINT64_FMT ")."),
			*CacheStore.NodeName, Requests.Num(), Results.Num());
		for (const RequestType& Request : Requests)
		{
			UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache miss with invalid response for %ls from '%ls'",
				*CacheStore.NodeName, *WriteToString<96>(Request.Key), *Request.Name);
			RequestStats.Bucket = Request.Key.Bucket;
			EndRequest(Request, {}, EStatus::Error);
		}
		return;
	}

	for (FCbFieldView ResultField : Results)
	{
		const FCbObjectView ResultObject = ResultField.AsObjectView();
		const uint32 OpId = ResultObject[ANSITEXTVIEW("opId")].AsUInt32();
		const int32 StatusCode = ResultObject[ANSITEXTVIEW("statusCode")].AsInt32();
		const FCbObjectView ResponseItemView = ResultObject[ANSITEXTVIEW("response")].AsObjectView();

		if (OpId >= (uint32)Requests.Num())
		{
			UE_LOGF(LogDerivedDataCache, Display, "%ls: Encountered invalid opId %d while querying %d values",
				*CacheStore.NodeName, OpId, Requests.Num());
			continue;
		}

		const RequestType& Request = Requests[int32(OpId)];
		RequestStats.Bucket = Request.Key.Bucket;

		if (StatusCode < 200 || StatusCode > 204)
		{
			UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache miss with unsuccessful response code %d for %ls from '%ls'",
				*CacheStore.NodeName, StatusCode, *WriteToString<96>(Request.Key), *Request.Name);
			EndRequest(Request, {}, EStatus::Error);
			continue;
		}

		EndRequest(Request, ResponseItemView, EStatus::Ok);
	}
}

template <>
void FHttpCacheStore::FExistsRecordBatchOp::EndRequest(const FCacheGetRequest& Request, const FCbObjectView ResponseView, EStatus Status)
{
	FOptionalCacheRecord Record;
	if (Status == EStatus::Ok && ResponseView && !LoadFromCompactBinary(ResponseView.AsFieldView(), Record))
	{
		UE_LOGF(LogDerivedDataCache, Log, "%ls: Cache miss with record load failure for %ls from '%ls'",
			*CacheStore.NodeName, *WriteToString<96>(Request.Key), *Request.Name);
		Status = EStatus::Error;
	}

	RequestStats.Type = ERequestType::Record;
	RequestStats.EndTime = FMonotonicTimePoint::Now();
	RequestStats.Status = Status;
	if (Record.IsNull())
	{
		Record = FCacheRecord(Request.Key);
	}
	OnComplete({Request.Name, MoveTemp(Record).Get(), Request.UserData, Status});
	if (CacheStore.StoreStats)
	{
		CacheStore.StoreStats->AddRequest(RequestStats);
	}
}

template <>
void FHttpCacheStore::FExistsValueBatchOp::EndRequest(const FCacheGetValueRequest& Request, const FCbObjectView ResponseView, EStatus Status)
{
	FValue Value;
	if (Status == EStatus::Ok && ResponseView)
	{
		const FIoHash RawHash = ResponseView[ANSITEXTVIEW("RawHash")].AsHash();
		const uint64 RawSize = ResponseView[ANSITEXTVIEW("RawSize")].AsUInt64(MAX_uint64);
		if (RawHash.IsZero() || RawSize == MAX_uint64)
		{
			UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache miss with invalid value for %ls from '%ls'",
				*CacheStore.NodeName, *WriteToString<96>(Request.Key), *Request.Name);
			Status = EStatus::Error;
		}
		else
		{
			Value = FValue(RawHash, RawSize);
		}
	}

	RequestStats.Type = ERequestType::Value;
	RequestStats.EndTime = FMonotonicTimePoint::Now();
	RequestStats.Status = Status;
	OnComplete({Request.Name, Request.Key, Value, Request.UserData, Status});
	if (CacheStore.StoreStats)
	{
		CacheStore.StoreStats->AddRequest(RequestStats);
	}
}

template <>
void FHttpCacheStore::FExistsChunkBatchOp::EndRequest(const FCacheGetChunkRequest& Request, const FCbObjectView ResponseView, EStatus Status)
{
	if (Request.Id.IsValid())
	{
		FValueWithId Value;
		FOptionalCacheRecord Record;
		if (Status == EStatus::Ok && ResponseView && !LoadFromCompactBinary(ResponseView.AsFieldView(), Record))
		{
			UE_LOGF(LogDerivedDataCache, Log, "%ls: Cache miss with record load failure for %ls from '%ls'",
				*CacheStore.NodeName, *WriteToString<96>(Request.Key), *Request.Name);
			Status = EStatus::Error;
		}
		else if (Record.IsValid())
		{
			Value = Record.Get().GetValue(Request.Id);
			if (!Value || Value.GetRawHash().IsZero())
			{
				UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache miss with missing/invalid value %ls for %ls from '%ls'",
					*CacheStore.NodeName, *WriteToString<24>(Request.Id), *WriteToString<96>(Request.Key), *Request.Name);
				Status = EStatus::Error;
			}
			else if (!Request.RawHash.IsZero() && Request.RawHash != Value.GetRawHash())
			{
				UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache miss with mismatched value %ls received hash %ls when expected hash %ls for %ls from '%ls'",
					*CacheStore.NodeName, *WriteToString<24>(Request.Id), *WriteToString<40>(Value.GetRawHash()), *WriteToString<40>(Request.RawHash), *WriteToString<96>(Request.Key), *Request.Name);
				Value.Reset();
				Status = EStatus::Error;
			}
		}

		const uint64 LogicalRawOffset = FMath::Min(Value.GetRawSize(), Request.RawOffset);
		const uint64 ResponseRawSize = FMath::Min(Value.GetRawSize() - LogicalRawOffset, Request.RawSize);

		RequestStats.Type = ERequestType::Record;
		RequestStats.EndTime = FMonotonicTimePoint::Now();
		RequestStats.Status = Status;
		OnComplete({Request.Name, Request.Key, Request.Id, Request.RawOffset, ResponseRawSize, Value.GetRawHash(), {}, Request.UserData, Status});
		if (CacheStore.StoreStats)
		{
			CacheStore.StoreStats->AddRequest(RequestStats);
		}
	}
	else
	{
		FValue Value;
		if (Status == EStatus::Ok && ResponseView)
		{
			const FIoHash RawHash = ResponseView[ANSITEXTVIEW("RawHash")].AsHash();
			const uint64 RawSize = ResponseView[ANSITEXTVIEW("RawSize")].AsUInt64(MAX_uint64);
			if (RawHash.IsZero() || RawSize == MAX_uint64)
			{
				UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache miss with invalid value %ls for %ls from '%ls'",
					*CacheStore.NodeName, *WriteToString<24>(Request.Id), *WriteToString<96>(Request.Key), *Request.Name);
				Status = EStatus::Error;
			}
			else if (!Request.RawHash.IsZero() && Request.RawHash != RawHash)
			{
				UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache miss with mismatched value received hash %ls when expected hash %ls for %ls from '%ls'",
					*CacheStore.NodeName, *WriteToString<40>(RawHash), *WriteToString<40>(Request.RawHash), *WriteToString<96>(Request.Key), *Request.Name);
				Status = EStatus::Error;
			}
			else
			{
				Value = FValue(RawHash, RawSize);
			}
		}
		
		const uint64 LogicalRawOffset = FMath::Min(Value.GetRawSize(), Request.RawOffset);
		const uint64 ResponseRawSize = FMath::Min(Value.GetRawSize() - LogicalRawOffset, Request.RawSize);

		RequestStats.Type = ERequestType::Value;
		RequestStats.EndTime = FMonotonicTimePoint::Now();
		RequestStats.Status = Status;
		OnComplete({Request.Name, Request.Key, Request.Id, Request.RawOffset, ResponseRawSize, Value.GetRawHash(), {}, Request.UserData, Status});
		if (CacheStore.StoreStats)
		{
			CacheStore.StoreStats->AddRequest(RequestStats);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FHttpCacheStore::FHttpCacheStore(const FHttpCacheStoreParams& Params, ICacheStoreOwner& Owner)
	: NodeName(Params.Name)
	, Domain(Params.Host)
	, Namespace(Params.Namespace)
	, OAuthProvider(Params.OAuthProvider)
	, OAuthClientId(Params.OAuthClientId)
	, OAuthSecret(Params.OAuthSecret)
	, OAuthScope(Params.OAuthScope)
	, OAuthProviderIdentifier(Params.OAuthProviderIdentifier)
	, OAuthAccessToken(Params.OAuthAccessToken)
	, HttpVersion(Params.HttpVersion)
	, UnixSocketPath(Params.UnixSocketPath)
	, AuthScheme(Params.AuthScheme)
	, StoreOwner(Owner)
	, bReadOnly(Params.bReadOnly)
	, bWriteOnly(Params.bWriteOnly)
	, bBypassProxy(Params.bBypassProxy)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_Construct);

	// Remove any trailing / because constructing a URI will add one.
	while (Domain.RemoveFromEnd(TEXT("/")));

	EffectiveDomain = WriteToAnsiString<128>(Domain);

	TAnsiStringBuilder<256> ResolvedHost;
	double ResolvedLatency;
	FHttpHostBuilder HostBuilder;
	HostBuilder.AddFromString(EffectiveDomain);
	if (!Params.DiscoveryHost.IsEmpty())
	{
		TAnsiStringBuilder<256> DiscoveryHost;
		DiscoveryHost.Append(Params.DiscoveryHost);
		AcquireAccessToken();
		HostBuilder.AddFromEndpoint(DiscoveryHost, WriteToAnsiString<1024>(*Access));
	}
	if (HostBuilder.ResolveHost(/* Warning timeout */ 1.0, 4.0 /* Max duration timeout*/, ResolvedHost, ResolvedLatency))
	{
		EffectiveDomain = ResolvedHost;
	}
	else
	{
		// even if we fail to resolve a host to use the returned host will at least contain the first of the possible hosts which we can attempt to use
		EffectiveDomain = ResolvedHost;

		FString HostCandidates = HostBuilder.GetHostCandidatesString();
		UE_LOGF(LogDerivedDataCache, Warning, "%ls: Unable to resolve best host candidate to use, most likely none of the suggested hosts was reachable. Attempted hosts were: '%ls' .", *NodeName, *HostCandidates);
	}

	TAnsiStringBuilder<256> ResolvedDomain;
	if (Params.bResolveHostCanonicalName && TryResolveCanonicalHost(EffectiveDomain, ResolvedDomain))
	{
		// Store the URI with the canonical name to pin to one region when using DNS-based region selection.
		UE_LOGF(LogDerivedDataCache, Display,
			"%ls: Pinned to %s based on DNS canonical name.", *NodeName, *ResolvedDomain);
		EffectiveDomain = ResolvedDomain;
	}

	UE_LOGF(LogDerivedDataCache, Display, "%ls: Using session id %ls.", *NodeName, *WriteToString<64>(FApp::GetSessionObjectId()));

#if WITH_SSL
	if (!Params.HostPinnedPublicKeys.IsEmpty() && EffectiveDomain.StartsWith(ANSITEXTVIEW("https://")))
	{
		FSslModule::Get().GetCertificateManager().SetPinnedPublicKeys(FString(GetDomainFromUri(EffectiveDomain)), Params.HostPinnedPublicKeys);
	}
	if (!Params.OAuthPinnedPublicKeys.IsEmpty() && OAuthProvider.StartsWith(TEXT("https://")))
	{
		FSslModule::Get().GetCertificateManager().SetPinnedPublicKeys(FString(GetDomainFromUri(WriteToAnsiString<256>(OAuthProvider))), Params.OAuthPinnedPublicKeys);
	}
#endif

	constexpr uint32 MaxTotalConnections = 8;
	FHttpConnectionPoolParams ConnectionPoolParams;
	ConnectionPoolParams.MaxConnections = MaxTotalConnections;
	ConnectionPoolParams.MinConnections = MaxTotalConnections;
	ConnectionPool = IHttpManager::Get().CreateConnectionPool(ConnectionPoolParams);

	FHttpClientParams ClientParams = GetDefaultClientParams();

	THttpUniquePtr<IHttpClient> Client = ConnectionPool->CreateClient(ClientParams);
	FHealthCheckOp HealthCheck(*this, *Client, StoreOwner);
	if (AcquireAccessToken(Client.Get()) && HealthCheck.IsReady())
	{
		// Default rate limits for getting blobs
		ClientParams.MaxRequests = UE_HTTPDDC_GETBLOBS_REQUEST_POOL_SIZE;
		ClientParams.MinRequests = UE_HTTPDDC_GETBLOBS_REQUEST_POOL_SIZE;
		GetBlobsRequestQueue.Initialize(*ConnectionPool, ClientParams);

		// More generous rate limits for operations that are expected to be low byte
		// DOWNLOADED (not uploaded) count but can be delayed when the server stalls.
		// Put operations are configured with these more generous limits even when
		// putting high byte count items (eg: blobs).
		ClientParams.LowSpeedLimit = 1;
		ClientParams.LowSpeedTime = 60;
		ClientParams.MaxRequests = UE_HTTPDDC_GETREF_REQUEST_POOL_SIZE;
		ClientParams.MinRequests = UE_HTTPDDC_GETREF_REQUEST_POOL_SIZE;
		GetRefRequestQueue.Initialize(*ConnectionPool, ClientParams);
		ClientParams.MaxRequests = UE_HTTPDDC_EXISTS_REQUEST_POOL_SIZE;
		ClientParams.MinRequests = UE_HTTPDDC_EXISTS_REQUEST_POOL_SIZE;
		ExistsRequestQueue.Initialize(*ConnectionPool, ClientParams);
		ClientParams.MaxRequests = UE_HTTPDDC_PUTREF_REQUEST_POOL_SIZE;
		ClientParams.MinRequests = UE_HTTPDDC_PUTREF_REQUEST_POOL_SIZE;
		PutRefRequestQueue.Initialize(*ConnectionPool, ClientParams);
		ClientParams.MaxRequests = UE_HTTPDDC_PUTBLOBS_REQUEST_POOL_SIZE;
		ClientParams.MinRequests = UE_HTTPDDC_PUTBLOBS_REQUEST_POOL_SIZE;
		PutBlobsRequestQueue.Initialize(*ConnectionPool, ClientParams);
		ClientParams.MaxRequests = UE_HTTPDDC_PUTFINALIZE_REQUEST_POOL_SIZE;
		ClientParams.MinRequests = UE_HTTPDDC_PUTFINALIZE_REQUEST_POOL_SIZE;
		PutFinalizeRequestQueue.Initialize(*ConnectionPool, ClientParams);

		bIsUsable = true;

		ECacheStoreFlags Flags = ECacheStoreFlags::Remote;
		Flags |= Params.bWriteOnly ? ECacheStoreFlags::None : ECacheStoreFlags::Query;
		Flags |= Params.bReadOnly ? ECacheStoreFlags::None : ECacheStoreFlags::Store;
		TStringBuilder<256> Path(InPlace, ResolvedHost, TEXTVIEW(" ("), Namespace, TEXTVIEW(")"));
		StoreOwner.Add(NodeName, this, Flags);
		StoreStats = StoreOwner.CreateStats(this, Flags, TEXTVIEW("Unreal Cloud DDC"), Params.Name, Path);

		StoreStats->SetAttribute(TEXTVIEW("Domain"), Domain);
		StoreStats->SetAttribute(TEXTVIEW("ResolvedDomain"), WriteToString<128>(ResolvedHost));
		StoreStats->SetAttribute(TEXTVIEW("EffectiveDomain"), WriteToString<128>(EffectiveDomain));
		StoreStats->SetAttribute(TEXTVIEW("Namespace"), Namespace);
		StoreStats->SetAttribute(TEXTVIEW("LoginAttempts"), WriteToString<16>(LoginAttempts));
		StoreStats->SetAttribute(TEXTVIEW("InteractiveLoginAttempts"), WriteToString<16>(InteractiveLoginAttempts));
		StoreStats->SetAttribute(TEXTVIEW("FailedLoginAttempts"), WriteToString<16>(FailedLoginAttempts));
	}

	AnyInstance = this;
}

FHttpCacheStore::~FHttpCacheStore()
{
	if (RefreshAccessTokenHandle.IsValid())
	{
		FTSTicker::RemoveTicker(RefreshAccessTokenHandle);
	}

	if (StoreStats)
	{
		StoreOwner.DestroyStats(StoreStats);
	}

	if (AnyInstance == this)
	{
		AnyInstance = nullptr;
	}
}

template <typename CharType>
static bool HttpVersionFromString(EHttpVersion& OutVersion, const TStringView<CharType> String)
{
	const auto ConvertedString = StringCast<UTF8CHAR, 16>(String.GetData(), String.Len());
	if (ConvertedString == UTF8TEXTVIEW("none"))
	{
		OutVersion = EHttpVersion::None;
	}
	else if (ConvertedString == UTF8TEXTVIEW("http1.0"))
	{
		OutVersion = EHttpVersion::V1_0;
	}
	else if (ConvertedString == UTF8TEXTVIEW("http1.1"))
	{
		OutVersion = EHttpVersion::V1_1;
	}
	else if (ConvertedString == UTF8TEXTVIEW("http2"))
	{
		OutVersion = EHttpVersion::V2;
	}
	else if (ConvertedString == UTF8TEXTVIEW("http2-only"))
	{
		OutVersion = EHttpVersion::V2Only;
	}
	else
	{
		return false;
	}
	return true;
}

bool TryLexFromString(EHttpVersion& OutVersion, FUtf8StringView String) { return HttpVersionFromString(OutVersion, String); }
bool TryLexFromString(EHttpVersion& OutVersion, FWideStringView String) { return HttpVersionFromString(OutVersion, String); }

FHttpClientParams FHttpCacheStore::GetDefaultClientParams() const
{
	FHttpClientParams ClientParams;
	ClientParams.DnsCacheTimeout = 15;
	ClientParams.ConnectTimeoutMs = 3 * 1000;
	ClientParams.LowSpeedLimit = 1024;
	ClientParams.LowSpeedTime = 10;
	ClientParams.TlsLevel = EHttpTlsLevel::All;
	ClientParams.bFollowRedirects = true;
	ClientParams.bFollow302Post = true;
	ClientParams.bBypassProxy = bBypassProxy;

	EHttpVersion HttpVersionEnum = EHttpVersion::V2;
	TryLexFromString(HttpVersionEnum, HttpVersion);
	ClientParams.Version = HttpVersionEnum;

	return ClientParams;
}

bool FHttpCacheStore::AcquireAccessToken(IHttpClient* Client)
{
	if (Domain.StartsWith(TEXT("http://localhost")))
	{
		UE_LOGF(LogDerivedDataCache, Log, "%ls: Skipping authorization for connection to localhost.", *NodeName);
		return true;
	}

	ON_SCOPE_EXIT
	{
		if (StoreStats)
		{
			StoreStats->SetAttribute(TEXTVIEW("LoginAttempts"), WriteToString<16>(LoginAttempts));
			StoreStats->SetAttribute(TEXTVIEW("InteractiveLoginAttempts"), WriteToString<16>(InteractiveLoginAttempts));
			StoreStats->SetAttribute(TEXTVIEW("FailedLoginAttempts"), WriteToString<16>(FailedLoginAttempts));
		}
	};

	LoginAttempts++;

	// Avoid spamming this if the service is down.
	if (FailedLoginAttempts > UE_HTTPDDC_MAX_FAILED_LOGIN_ATTEMPTS)
	{
		return false;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_AcquireAccessToken);

	// In case many requests wants to update the token at the same time
	// get the current serial while we wait to take the CS.
	const uint32 WantsToUpdateTokenSerial = Access ? Access->GetSerial() : 0;

	FScopeLock Lock(&AccessCs);

	// If the token was updated while we waited to take the lock, then it should now be valid.
	if (Access && Access->GetSerial() > WantsToUpdateTokenSerial)
	{
		return true;
	}

	if (!OAuthAccessToken.IsEmpty())
	{
		SetAccessTokenAndUnlock(Lock, OAuthAccessToken);
		return true;
	}

	if (!OAuthSecret.IsEmpty())
	{
		THttpUniquePtr<IHttpClient> LocalClient;
		if (!Client)
		{
			LocalClient = ConnectionPool->CreateClient(GetDefaultClientParams());
			Client = LocalClient.Get();
		}

		FHttpRequestParams RequestParams;
		RequestParams.bIgnoreMaxRequests = true;
		FHttpOperation Operation(Client->TryCreateRequest(RequestParams), StoreOwner);
		Operation.SetUri(StringCast<ANSICHAR>(*OAuthProvider));

		if (OAuthProvider.StartsWith(TEXT("http://localhost")))
		{
			// Simple unauthenticated call to a local endpoint that mimics the result from an OIDC provider.
			Operation.Send();
		}
		else
		{
			TUtf8StringBuilder<256> OAuthFormData;
			OAuthFormData
				<< ANSITEXTVIEW("client_id=") << OAuthClientId
				<< ANSITEXTVIEW("&scope=") << OAuthScope
				<< ANSITEXTVIEW("&grant_type=client_credentials")
				<< ANSITEXTVIEW("&client_secret=") << OAuthSecret;

			Operation.SetMethod(EHttpMethod::Post);
			Operation.SetContentType(EHttpMediaType::FormUrlEncoded);
			Operation.SetBody(FCompositeBuffer(FSharedBuffer::MakeView(MakeMemoryView(OAuthFormData))));
			Operation.Send();
		}

		if (Operation.GetStatusCode() == 200)
		{
			if (TSharedPtr<FJsonObject> ResponseObject = Operation.GetBodyAsJson())
			{
				FString AccessTokenString;
				double ExpiryTimeSeconds = 0.0;
				if (ResponseObject->TryGetStringField(TEXT("access_token"), AccessTokenString) &&
					ResponseObject->TryGetNumberField(TEXT("expires_in"), ExpiryTimeSeconds))
				{
					UE_LOGF(LogDerivedDataCache, Display,
						"%ls: Logged in to HTTP DDC services. Expires in %.0f seconds.", *NodeName, ExpiryTimeSeconds);
					SetAccessTokenAndUnlock(Lock, AccessTokenString, ExpiryTimeSeconds);
					return true;
				}
			}
		}

		UE_LOGF(LogDerivedDataCache, Warning, "%ls: Failed to log in to HTTP services with request %ls.", *NodeName, *WriteToString<256>(Operation));
		FailedLoginAttempts++;
		return false;
	}

	if (!OAuthProviderIdentifier.IsEmpty())
	{
		FString AccessTokenString;
		FDateTime TokenExpiresAt;
		bool bWasInteractiveLogin = false;

		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::TryGet();
		EDesktopLoginInteractionLevel InteractionLevel = FApp::IsUnattended() ? EDesktopLoginInteractionLevel::TimeLimited : EDesktopLoginInteractionLevel::Interactive;
		if (DesktopPlatform && DesktopPlatform->GetOidcAccessToken(FPaths::RootDir(), FPaths::GetProjectFilePath(), OAuthProviderIdentifier, InteractionLevel, GWarn, AccessTokenString, TokenExpiresAt, bWasInteractiveLogin))
		{
			if (bWasInteractiveLogin)
			{
				InteractiveLoginAttempts++;
			}

			const double ExpiryTimeSeconds = (TokenExpiresAt - FDateTime::UtcNow()).GetTotalSeconds();
			UE_LOGF(LogDerivedDataCache, Display,
				"%ls: OidcToken: Logged in to HTTP DDC services. Expires at %ls which is in %.0f seconds.",
				*NodeName, *TokenExpiresAt.ToString(), ExpiryTimeSeconds);
			SetAccessTokenAndUnlock(Lock, AccessTokenString, ExpiryTimeSeconds);
			return true;
		}
		else if (DesktopPlatform)
		{
			UE_LOGF(LogDerivedDataCache, Warning, "%ls: OidcToken: Failed to log in to HTTP services.", *NodeName);
			FailedLoginAttempts++;
			return false;
		}
		else
		{
			UE_LOGF(LogDerivedDataCache, Warning, "%ls: OidcToken: Use of OAuthProviderIdentifier requires that the target depend on DesktopPlatform.", *NodeName);
			FailedLoginAttempts++;
			return false;
		}
	}

	UE_LOGF(LogDerivedDataCache, Warning, "%ls: No available configuration to acquire an access token.", *NodeName);
	FailedLoginAttempts++;
	return false;
}

void FHttpCacheStore::SetAccessTokenAndUnlock(FScopeLock& Lock, FStringView Token, double RefreshDelay)
{
	// Cache the expired refresh handle.
	FTSTicker::FDelegateHandle ExpiredRefreshAccessTokenHandle = MoveTemp(RefreshAccessTokenHandle);
	RefreshAccessTokenHandle.Reset();

	if (!Access)
	{
		Access = MakeUnique<FHttpAccessToken>();
	}
	Access->SetToken(AuthScheme, Token);

	constexpr double RefreshGracePeriod = 20.0f;
	if (RefreshDelay > RefreshGracePeriod)
	{
		// Schedule a refresh of the token ahead of expiry time (this will not work in commandlets)
		if (!IsRunningCommandlet())
		{
			RefreshAccessTokenHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
				[this](float DeltaTime)
				{
					AcquireAccessToken();
					return false;
				}
			), float(FMath::Min(RefreshDelay - RefreshGracePeriod, MAX_flt)));
		}

		// Schedule a forced refresh of the token when the scheduled refresh is starved or unavailable.
		RefreshAccessTokenTime = FPlatformTime::Seconds() + RefreshDelay - RefreshGracePeriod * 0.5f;
	}
	else
	{
		RefreshAccessTokenTime = 0.0;
	}

	// Reset failed login attempts, the service is indeed alive.
	FailedLoginAttempts = 0;

	// Unlock the critical section before attempting to remove the expired refresh handle.
	// The associated ticker delegate could already be executing, which could cause a
	// hang in RemoveTicker when the critical section is locked.
	Lock.Unlock();
	if (ExpiredRefreshAccessTokenHandle.IsValid())
	{
		FTSTicker::RemoveTicker(MoveTemp(ExpiredRefreshAccessTokenHandle));
	}
}

FHttpRequestQueue& FHttpCacheStore::PickRequestQueue(EOperationCategory Category)
{
	switch (Category)
	{
	case EOperationCategory::GetRef:
		return GetRefRequestQueue;
	case EOperationCategory::GetBlobs:
		return GetBlobsRequestQueue;
	case EOperationCategory::Exists:
		return ExistsRequestQueue;
	case EOperationCategory::PutRef:
		return PutRefRequestQueue;
	case EOperationCategory::PutBlobs:
		return PutBlobsRequestQueue;
	case EOperationCategory::PutFinalize:
		return PutFinalizeRequestQueue;
	default:
		checkNoEntry();
		return GetBlobsRequestQueue;
	}
}

void FHttpCacheStore::WaitForHttpOperationAsync(IRequestOwner& Owner, EOperationCategory Category, TUniqueFunction<void (TUniquePtr<FHttpOperation>&&)>&& OnOperation)
{
	WaitForHttpRequestAsync(Owner, Category, [this, OnOperation = MoveTemp(OnOperation)](THttpUniquePtr<IHttpRequest>&& Request)
	{
		if (UNLIKELY(!Request))
		{
			OnOperation({});
			return;
		}

		if (Access && RefreshAccessTokenTime > 0.0 && RefreshAccessTokenTime < FPlatformTime::Seconds())
		{
			AcquireAccessToken();
		}

		if (Access)
		{
			Request->AddHeader(ANSITEXTVIEW("Authorization"), WriteToAnsiString<1024>(*Access));
		}

		OnOperation(MakeUnique<FHttpOperation>(MoveTemp(Request), StoreOwner));
	});
}

void FHttpCacheStore::WaitForHttpRequestAsync(IRequestOwner& Owner, EOperationCategory Category, TUniqueFunction<void (THttpUniquePtr<IHttpRequest>&&)>&& OnRequest)
{
	FHttpRequestParams Params;
	FHttpRequestQueue& RequestQueue = PickRequestQueue(Category);
	RequestQueue.CreateRequestAsync(Owner, Params, MoveTemp(OnRequest));
}

void FHttpCacheStore::PutCacheRecordAsync(IRequestOwner& Owner, const FCachePutRequest& Request, FOnCachePutComplete&& OnComplete)
{
	const FCacheKey& Key = Request.Record.GetKey();

	if (bReadOnly)
	{
		UE_LOGF(LogDerivedDataCache, VeryVerbose,
			"%ls: Skipped put of %ls from '%ls' because this cache store is read-only",
			*NodeName, *WriteToString<96>(Key), *Request.Name);
		return OnComplete(Request.MakeResponse(EStatus::Error));
	}

	// Skip the request if storing to the cache is disabled.
	const ECachePolicy RecordPolicy = Request.Policy.GetRecordPolicy();
	if (!EnumHasAnyFlags(RecordPolicy, ECachePolicy::StoreRemote))
	{
		UE_LOGF(LogDerivedDataCache, VeryVerbose, "%ls: Skipped put of %ls from '%ls' due to cache policy",
			*NodeName, *WriteToString<96>(Key), *Request.Name);
		return OnComplete(Request.MakeResponse(EStatus::Error));
	}

	TRefCountPtr<FPutPackageOp> Op = FPutPackageOp::New(*this, Owner, Request.Name);

	FCbPackage Package;
	{
		FRequestStats& RequestStats = Op->EditStats();
		RequestStats.Bucket = Key.Bucket;
		RequestStats.Type = ERequestType::Record;
		RequestStats.Op = ERequestOp::Put;

		FRequestTimer RequestTimer(RequestStats);
		Package = Request.Record.Save();
	}

	if (!EnumHasAnyFlags(RecordPolicy, ECachePolicy::QueryRemote))
	{
		// Track keys put with an overwrite policy to force consistent read on subsequent gets.
		TUniqueLock Lock(OverwriteKeysMutex);
		OverwriteKeys.Add(Key);
	}

	Op->Put(Key, Request.Policy, MoveTemp(Package), [this, Op, Request, OnComplete = MoveTemp(OnComplete)](FPutPackageOp::FResponse&& OpResponse) mutable
	{
		if (OpResponse.Status == EStatus::Ok)
		{
			if (const FCbObject& Meta = Request.Record.GetMeta())
			{
				Op->EditStats().LogicalWriteSize += Meta.GetSize();
			}
		}

		TRACE_COUNTER_ADD(HttpDDC_BytesReceived, Op->ReadStats().PhysicalReadSize);
		TRACE_COUNTER_ADD(HttpDDC_BytesSent, Op->ReadStats().PhysicalWriteSize);

		FCachePutResponse Response = Request.MakeResponse(OpResponse.Status);

		if (FOptionalCacheRecord ExistingRecord; OpResponse.ExistingObject && LoadFromCompactBinary(OpResponse.ExistingObject.AsFieldView(), ExistingRecord))
		{
			// Report puts of non-deterministic records by ignoring known-non-deterministic values.
			const auto MakeValueTuple = [&Request](const FValueWithId& Value) -> TTuple<FValueId, FIoHash>
			{
				return !EnumHasAnyFlags(Request.Policy.GetValuePolicy(Value.GetId()), ECachePolicy::NonDeterministic)
					? MakeTuple(Value.GetId(), Value.GetRawHash())
					: TTuple<FValueId, FIoHash>{};
			};
			if (!Algo::CompareBy(ExistingRecord.Get().GetValues(), Request.Record.GetValues(), MakeValueTuple))
			{
				UE_LOGF(LogDerivedDataCache, Display, "%ls: Cache put found non-deterministic record for %ls from '%ls'",
					*NodeName, *WriteToString<96>(Request.Record.GetKey()), *Request.Name);
			}

			// Compare the whole record to check whether the record from the cache needs to be returned.
			if (!Algo::Compare(ExistingRecord.Get().GetValues(), Request.Record.GetValues()))
			{
				Response.Record = ExistingRecord.Get();
			}

			// Compare the metadata to check whether the metadata from the cache needs to be returned.
			if (!ExistingRecord.Get().GetMeta().Equals(Response.Record.GetMeta()))
			{
				Response.Record = FCacheRecord::CreateByMove(Response.Record.GetKey(), CopyTemp(ExistingRecord.Get().GetMeta()),
					TArray<FValueWithId>(Response.Record.GetValues()));
			}
		}

		if (EnumHasAnyFlags(Request.Policy.GetRecordPolicy(), ECachePolicy::SkipData) ||
			Algo::AllOf(Response.Record.GetValues(), [&Request](const FValueWithId& Value)
			{
				return Value.HasData() || EnumHasAnyFlags(Request.Policy.GetValuePolicy(Value.GetId()), ECachePolicy::SkipData);
			}))
		{
			return OnComplete(MoveTemp(Response));
		}

		IRequestOwner& Owner = Op->GetOwner();
		FRequestBarrier Barrier(Owner);
		const FCacheRecordPolicy Policy = Request.Policy.Transform([](ECachePolicy P) { return P | ECachePolicy::Query; });
		GetCacheRecordAsync(Owner, Request.Name, Request.Record.GetKey(), Policy, Request.UserData,
			[OnComplete = MoveTemp(OnComplete)](FCacheGetResponse&& Response)
			{
				PRAGMA_DISABLE_DEPRECATION_WARNINGS;
				OnComplete({Response.Name, Response.Record.GetKey(), Response.Record, Response.UserData, Response.Status});
				PRAGMA_ENABLE_DEPRECATION_WARNINGS;
			});
	});
}

void FHttpCacheStore::PutCacheValueAsync(IRequestOwner& Owner, const FCachePutValueRequest& Request, FOnCachePutValueComplete&& OnComplete)
{
	if (bReadOnly)
	{
		UE_LOGF(LogDerivedDataCache, VeryVerbose,
			"%ls: Skipped put of %ls from '%ls' because this cache store is read-only",
			*NodeName, *WriteToString<96>(Request.Key), *Request.Name);
		return OnComplete(Request.MakeResponse(EStatus::Error));
	}

	// Skip the request if storing to the cache is disabled.
	if (!EnumHasAnyFlags(Request.Policy, ECachePolicy::StoreRemote))
	{
		UE_LOGF(LogDerivedDataCache, VeryVerbose, "%ls: Skipped put of %ls from '%ls' due to cache policy",
			*NodeName, *WriteToString<96>(Request.Key), *Request.Name);
		return OnComplete(Request.MakeResponse(EStatus::Error));
	}

	TRefCountPtr<FPutPackageOp> Op = FPutPackageOp::New(*this, Owner, Request.Name);

	FCbPackage Package;
	{
		FRequestStats& RequestStats = Op->EditStats();
		RequestStats.Bucket = Request.Key.Bucket;
		RequestStats.Type = ERequestType::Value;
		RequestStats.Op = ERequestOp::Put;

		FRequestTimer RequestTimer(RequestStats);

		FCbWriter Writer;
		SaveToCompactBinary(Writer, Request.Value, [&Package](FCbAttachment&& Attachment) { Package.AddAttachment(MoveTemp(Attachment)); });
		Package.SetObject(Writer.Save().AsObject());
	}

	if (!EnumHasAnyFlags(Request.Policy, ECachePolicy::QueryRemote))
	{
		// Track keys put with an overwrite policy to force consistent read on subsequent gets.
		TUniqueLock Lock(OverwriteKeysMutex);
		OverwriteKeys.Add(Request.Key);
	}

	Op->Put(Request.Key, Request.Policy, MoveTemp(Package), [this, Op, Request, OnComplete = MoveTemp(OnComplete)](FPutPackageOp::FResponse&& OpResponse) mutable
	{
		TRACE_COUNTER_ADD(HttpDDC_BytesReceived, Op->ReadStats().PhysicalReadSize);
		TRACE_COUNTER_ADD(HttpDDC_BytesSent, Op->ReadStats().PhysicalWriteSize);

		FCachePutValueResponse Response = Request.MakeResponse(OpResponse.Status);

		if (FValue ExistingValue; OpResponse.ExistingObject && LoadFromCompactBinary(OpResponse.ExistingObject.AsFieldView(), ExistingValue))
		{
			UE_CLOGF(!EnumHasAnyFlags(Request.Policy, ECachePolicy::NonDeterministic),
				LogDerivedDataCache, Display, "%ls: Cache put found non-deterministic value for %ls from '%ls'",
				*NodeName, *WriteToString<96>(Request.Key), *Request.Name);
			Response.Value = ExistingValue;
		}

		if (EnumHasAnyFlags(Request.Policy, ECachePolicy::SkipData) || Response.Value.HasData())
		{
			return OnComplete(MoveTemp(Response));
		}

		IRequestOwner& Owner = Op->GetOwner();
		FRequestBarrier Barrier(Owner);
		GetCacheValueAsync(Owner, Request.Name, Request.Key, Request.Policy | ECachePolicy::Query, ERequestOp::Put, Request.UserData,
			[OnComplete = MoveTemp(OnComplete)](FCacheGetValueResponse&& Response)
			{
				OnComplete({Response.Name, Response.Key, Response.Value, Response.UserData, Response.Status});
			});
	});
}

void FHttpCacheStore::GetCacheValueAsync(
	IRequestOwner& Owner,
	FSharedString Name,
	const FCacheKey& Key,
	ECachePolicy Policy,
	ERequestOp RequestOp,
	uint64 UserData,
	FOnCacheGetValueComplete&& OnComplete)
{
	if (!IsUsable())
	{
		UE_LOGF(LogDerivedDataCache, VeryVerbose,
			"%ls: Skipped get of %ls from '%ls' because this cache store is not available",
			*NodeName, *WriteToString<96>(Key), *Name);
		OnComplete({Name, Key, {}, UserData, EStatus::Error});
		return;
	}

	// Skip the request if querying the cache is disabled.
	if (!EnumHasAnyFlags(Policy, ECachePolicy::QueryRemote))
	{
		UE_LOGF(LogDerivedDataCache, VeryVerbose, "%ls: Skipped get of %ls from '%ls' due to cache policy",
			*NodeName, *WriteToString<96>(Key), *Name);
		OnComplete({Name, Key, {}, UserData, EStatus::Error});
		return;
	}

	TRefCountPtr<FGetValueOp> Op = FGetValueOp::New(*this, Owner, Name);

	FRequestStats& RequestStats = Op->EditStats();
	RequestStats.Bucket = Key.Bucket;
	RequestStats.Type = ERequestType::Value;
	RequestStats.Op = RequestOp;

	Op->Get(Key, Policy, [Op, UserData, OnComplete = MoveTemp(OnComplete)](FGetValueOp::FResponse&& Response)
	{
		TRACE_COUNTER_ADD(HttpDDC_BytesReceived, Op->ReadStats().PhysicalReadSize);
		TRACE_COUNTER_ADD(HttpDDC_BytesSent, Op->ReadStats().PhysicalWriteSize);
		OnComplete({Response.Name, Response.Key, MoveTemp(Response.Value), UserData, Response.Status});
	});
}

void FHttpCacheStore::GetCacheRecordAsync(
	IRequestOwner& Owner,
	const FSharedString& Name,
	const FCacheKey& Key,
	const FCacheRecordPolicy& Policy,
	uint64 UserData,
	FOnCacheGetComplete&& OnComplete)
{
	TRefCountPtr<FGetRecordOp> Op = FGetRecordOp::New(*this, Owner, Name);

	FRequestStats& RequestStats = Op->EditStats();
	RequestStats.Bucket = Key.Bucket;
	RequestStats.Type = ERequestType::Record;
	RequestStats.Op = ERequestOp::Get;

	Op->GetRecord(Key, Policy, [Op, Name, UserData, OnComplete = MoveTemp(OnComplete)](FGetRecordOp::FRecordResponse&& Response)
	{
		if (Response.Status == EStatus::Ok)
		{
			if (const FCbObject& Meta = Response.Record.GetMeta())
			{
				Op->EditStats().LogicalReadSize += Meta.GetSize();
			}
		}
		Op->RecordStats(Response.Status);
		TRACE_COUNTER_ADD(HttpDDC_BytesReceived, Op->ReadStats().PhysicalReadSize);
		TRACE_COUNTER_ADD(HttpDDC_BytesSent, Op->ReadStats().PhysicalWriteSize);
		OnComplete({Name, MoveTemp(Response.Record), UserData, Response.Status});
	});
}

void FHttpCacheStore::FinishChunkRequest(
	const FCacheGetChunkRequest& Request,
	EStatus Status,
	const FValue& Value,
	FCompressedBufferReader& ValueReader,
	const TSharedRef<FOnCacheGetChunkComplete>& SharedOnComplete)
{
	if (Status == EStatus::Ok)
	{
		if (Request.RawHash.IsZero() || Request.RawHash == Value.GetRawHash())
		{
			const uint64 RawOffset = FMath::Min(Value.GetRawSize(), Request.RawOffset);
			const uint64 RawSize = FMath::Min(Value.GetRawSize() - RawOffset, Request.RawSize);
			UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache hit for %ls from '%ls'",
				*NodeName, *WriteToString<96>(Request.Key, '/', Request.Id), *Request.Name);
			FSharedBuffer Buffer;
			const bool bExistsOnly = EnumHasAnyFlags(Request.Policy, ECachePolicy::SkipData);
			if (!bExistsOnly)
			{
				Buffer = ValueReader.Decompress(RawOffset, RawSize);
			}
			const EStatus ChunkStatus = bExistsOnly || Buffer.GetSize() == RawSize ? EStatus::Ok : EStatus::Error;
			if (ChunkStatus == EStatus::Ok)
			{
				TRACE_COUNTER_INCREMENT(HttpDDC_GetHit);
			}
			SharedOnComplete.Get()({Request.Name, Request.Key, Request.Id, Request.RawOffset,
				RawSize, Value.GetRawHash(), MoveTemp(Buffer), Request.UserData, ChunkStatus});
		}
		else
		{
			UE_LOGFMT(LogDerivedDataCache, Verbose,
				"{Cache}: Cache miss with mismatched value {Id} received hash {ReceivedHash} when expected hash {RawHash} for {Key} from '{Name}'",
				NodeName, Request.Id, Value.GetRawHash(), Request.RawHash, Request.Key, Request.Name);
			SharedOnComplete.Get()(Request.MakeResponse(EStatus::Error));
		}
	}
	else
	{
		UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache miss for %ls from '%ls'",
			*NodeName, *WriteToString<96>(Request.Key, '/', Request.Id), *Request.Name);
		SharedOnComplete.Get()(Request.MakeResponse(Status));
	}
}

void FHttpCacheStore::GetChunkGroupAsync(
	IRequestOwner& Owner,
	const FCacheGetChunkRequest* StartRequest,
	const FCacheGetChunkRequest* EndRequest,
	TSharedRef<FOnCacheGetChunkComplete>& SharedOnComplete)
{
	if ((StartRequest == nullptr) || (StartRequest >= EndRequest))
	{
		return;
	}

	ECachePolicy GroupPolicy = ECachePolicy::SkipData | ECachePolicy::SkipMeta;
	TArray<FCacheGetChunkRequest> RequestGroup;
	RequestGroup.Reserve(static_cast<int>(EndRequest - StartRequest));
	for (const FCacheGetChunkRequest* Request = StartRequest; Request != EndRequest; ++Request)
	{
		RequestGroup.Add(*Request);
		GroupPolicy = CombineCachePolicy(GroupPolicy, Request->Policy);
	}

	if (StartRequest->Id.IsValid())
	{
		// Get Record and contained Values within the request group
		TRefCountPtr<FGetRecordOp> Op = FGetRecordOp::New(*this, Owner, StartRequest->Name);

		Op->GetRecordOnly(StartRequest->Key, GroupPolicy, [this, Op = TRefCountPtr(Op), RequestGroup = MoveTemp(RequestGroup), SharedOnComplete](FGetRecordOp::FRecordResponse&& Response) mutable
		{
			auto RecordStats = [](FGetRecordOp& Op, FCacheBucket Bucket, EStatus Status)
			{
				FRequestStats& RequestStats = Op.EditStats();
				RequestStats.Type = ERequestType::Record;
				RequestStats.Bucket = Bucket;
				RequestStats.Op = ERequestOp::GetChunk;
				Op.RecordStats(Status);
				TRACE_COUNTER_ADD(HttpDDC_BytesReceived, Op.ReadStats().PhysicalReadSize);
				TRACE_COUNTER_ADD(HttpDDC_BytesSent, Op.ReadStats().PhysicalWriteSize);
			};

			if (Response.Status == EStatus::Ok)
			{
				// Get Values on the record
				FRequestTimer RequestTimer(Op->EditStats());

				TArray<FValueWithId> RequiredGets;
				TArray<TArray<FCacheGetChunkRequest>> RequiredGetRequests;
				FCompressedBufferReader NullReader;
				for (const FCacheGetChunkRequest& Request : RequestGroup)
				{
					const FValueWithId& Value = Response.Record.GetValue(Request.Id);
					if (!Value || !EnumHasAnyFlags(Request.Policy, ECachePolicy::Query) || EnumHasAnyFlags(Request.Policy, ECachePolicy::SkipData))
					{
						const EStatus Status = Value ? Response.Status : EStatus::Error;
						FinishChunkRequest(Request, Status, Value, NullReader, SharedOnComplete);
					}
					else
					{
						const bool bAlreadyRequiredGet = !RequiredGets.IsEmpty() && RequiredGets.Last() == Value;

						if (!bAlreadyRequiredGet)
						{
							RequiredGets.Emplace(Value);
							RequiredGetRequests.AddDefaulted();
						}

						RequiredGetRequests.Last().Add(Request);
					}
				}

				int32 PendingValues = RequiredGets.Num();
				Op->PrepareForPendingValues(PendingValues);

				RequestTimer.Stop();

				if (PendingValues == 0)
				{
					RecordStats(*Op, RequestGroup[0].Key.Bucket, Response.Status);
					return;
				}

				Op->GetValues(RequiredGets, [this, RecordStats, Op = TRefCountPtr(Op), ChunkRequestsForValues = MoveTemp(RequiredGetRequests), SharedOnComplete](FGetRecordOp::FValueResponse&& Response)
				{
					int FoundRequestsIndex = Algo::BinarySearchBy(ChunkRequestsForValues, Response.Value.GetId(), [](const TArray<FCacheGetChunkRequest>& ChunkRequests)
					{
						check(!ChunkRequests.IsEmpty());
						return ChunkRequests[0].Id;
					});

					check(FoundRequestsIndex != INDEX_NONE);
					const TArray<FCacheGetChunkRequest>& ChunkRequests = ChunkRequestsForValues[FoundRequestsIndex];
					FCompressedBufferReader ValueReader(Response.Value.GetData());

					if (Op->FinishPendingValueFetch(Response.Value, false))
					{
						RecordStats(*Op, ChunkRequests[0].Key.Bucket, Op->GetFailedValues() > 0 ? EStatus::Error : EStatus::Ok);
					}

					for (const FCacheGetChunkRequest& ChunkRequest : ChunkRequests)
					{
						FinishChunkRequest(ChunkRequest, Response.Status, Response.Value, ValueReader, SharedOnComplete);
					}
				});
			}
			else
			{
				FCompressedBufferReader NullReader;
				FValue DummyValue;
				for (const FCacheGetChunkRequest& Request : RequestGroup)
				{
					FinishChunkRequest(Request, Response.Status, DummyValue, NullReader, SharedOnComplete);
				}

				RecordStats(*Op, RequestGroup[0].Key.Bucket, Response.Status);
			}
		});
	}
	else
	{
		// Get Value for the request group
		GetCacheValueAsync(Owner, StartRequest->Name, StartRequest->Key, GroupPolicy, ERequestOp::GetChunk, 0, [this, RequestGroup = MoveTemp(RequestGroup), SharedOnComplete](FCacheGetValueResponse&& Response)
		{
			FCompressedBufferReader ValueReader(Response.Value.GetData());
			for (const FCacheGetChunkRequest& Request : RequestGroup)
			{
				FinishChunkRequest(Request, Response.Status, Response.Value, ValueReader, SharedOnComplete);
			}
		});
	}
}

void FHttpCacheStore::LegacyStats(FDerivedDataCacheStatsNode& OutNode)
{
	checkNoEntry();
}

void FHttpCacheStore::Put(
	const TConstArrayView<FCachePutRequest> Requests,
	IRequestOwner& Owner,
	FOnCachePutComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_Put);
	TRACE_COUNTER_ADD(HttpDDC_Put, Requests.Num());

	FRequestBarrier Barrier(Owner);
	TSharedRef<FOnCachePutComplete> SharedOnComplete = MakeShared<FOnCachePutComplete>(MoveTemp(OnComplete));
	for (const FCachePutRequest& Request : Requests)
	{
		PutCacheRecordAsync(Owner, Request, [SharedOnComplete](FCachePutResponse&& Response)
		{
			if (Response.Status == EStatus::Ok)
			{
				TRACE_COUNTER_INCREMENT(HttpDDC_PutHit);
			}
			SharedOnComplete.Get()(MoveTemp(Response));
		});
	}
}

void FHttpCacheStore::Get(
	const TConstArrayView<FCacheGetRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_Get);
	TRACE_COUNTER_ADD(HttpDDC_Get, Requests.Num());

	const auto HasSkipData = [](const FCacheGetRequest& Request) { return EnumHasAnyFlags(Request.Policy.GetRecordPolicy(), ECachePolicy::SkipData); };

	TArray<FCacheGetRequest, TInlineAllocator<16>> ExistRequests;
	for (const FCacheGetRequest& Request : Requests)
	{
		if (HasSkipData(Request))
		{
			ExistRequests.Emplace(Request);
		}
	}

	FRequestBarrier Barrier(Owner);
	TSharedRef<FOnCacheGetComplete> SharedOnComplete = MakeShared<FOnCacheGetComplete>(MoveTemp(OnComplete));
	if (!ExistRequests.IsEmpty())
	{
		TRefCountPtr<FExistsRecordBatchOp> Op = FExistsRecordBatchOp::New(*this, Owner);
		Op->Exists(ExistRequests, [this, SharedOnComplete](FCacheGetResponse&& Response)
		{
			if (Response.Status == EStatus::Ok)
			{
				TRACE_COUNTER_INCREMENT(HttpDDC_GetHit);
				UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache hit for %ls from '%ls'",
					*NodeName, *WriteToString<96>(Response.Record.GetKey()), *Response.Name);
			}
			SharedOnComplete.Get()(MoveTemp(Response));
		});
	}

	for (const FCacheGetRequest& Request : Requests)
	{
		if (!HasSkipData(Request))
		{
			GetCacheRecordAsync(Owner, Request.Name, Request.Key, Request.Policy, Request.UserData,
				[this, SharedOnComplete](FCacheGetResponse&& Response)
			{
				if (Response.Status == EStatus::Ok)
				{
					TRACE_COUNTER_INCREMENT(HttpDDC_GetHit);
					UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache hit for %ls from '%ls'",
						*NodeName, *WriteToString<96>(Response.Record.GetKey()), *Response.Name);
				}
				SharedOnComplete.Get()(MoveTemp(Response));
			});
		}
	}
}

void FHttpCacheStore::PutValue(
	const TConstArrayView<FCachePutValueRequest> Requests,
	IRequestOwner& Owner,
	FOnCachePutValueComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_PutValue);
	TRACE_COUNTER_ADD(HttpDDC_Put, Requests.Num());

	FRequestBarrier Barrier(Owner);
	TSharedRef<FOnCachePutValueComplete> SharedOnComplete = MakeShared<FOnCachePutValueComplete>(MoveTemp(OnComplete));
	for (const FCachePutValueRequest& Request : Requests)
	{
		PutCacheValueAsync(Owner, Request, [SharedOnComplete](FCachePutValueResponse&& Response)
		{
			if (Response.Status == EStatus::Ok)
			{
				TRACE_COUNTER_INCREMENT(HttpDDC_PutHit);
			}
			SharedOnComplete.Get()(MoveTemp(Response));
		});
	}
}

void FHttpCacheStore::GetValue(
	const TConstArrayView<FCacheGetValueRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetValueComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_GetValue);
	TRACE_COUNTER_ADD(HttpDDC_Get, Requests.Num());

	const auto HasSkipData = [](const FCacheGetValueRequest& Request) { return EnumHasAnyFlags(Request.Policy, ECachePolicy::SkipData); };

	TArray<FCacheGetValueRequest, TInlineAllocator<16>> ExistRequests;
	for (const FCacheGetValueRequest& Request : Requests)
	{
		if (HasSkipData(Request))
		{
			ExistRequests.Emplace(Request);
		}
	}

	FRequestBarrier Barrier(Owner);
	TSharedRef<FOnCacheGetValueComplete> SharedOnComplete = MakeShared<FOnCacheGetValueComplete>(MoveTemp(OnComplete));
	if (!ExistRequests.IsEmpty())
	{
		TRefCountPtr<FExistsValueBatchOp> Op = FExistsValueBatchOp::New(*this, Owner);
		Op->Exists(ExistRequests, [this, SharedOnComplete](FCacheGetValueResponse&& Response)
		{
			if (Response.Status == EStatus::Ok)
			{
				TRACE_COUNTER_INCREMENT(HttpDDC_GetHit);
				UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache hit for %ls from '%ls'",
					*NodeName, *WriteToString<96>(Response.Key), *Response.Name);
			}
			SharedOnComplete.Get()(MoveTemp(Response));
		});
	}

	for (const FCacheGetValueRequest& Request : Requests)
	{
		if (!HasSkipData(Request))
		{
			GetCacheValueAsync(Owner, Request.Name, Request.Key, Request.Policy, ERequestOp::Get, Request.UserData,
			[this, Policy = Request.Policy, SharedOnComplete](FCacheGetValueResponse&& Response)
			{
				if (Response.Status == EStatus::Ok &&
					EnumHasAnyFlags(Policy, ECachePolicy::Query) &&
					!EnumHasAnyFlags(Policy, ECachePolicy::SkipData) &&
					!Response.Value.HasData())
				{
					Response.Status = EStatus::Error;
					// With inline fetching, expect we will always have a value we can use.
					// Even SkipData/Exists can rely on the blob existing if the ref is reported to exist.
					UE_LOGF(LogDerivedDataCache, Log, "%ls: Cache miss due to inlining failure for %ls from '%ls'",
						*NodeName, *WriteToString<96>(Response.Key), *Response.Name);
				}

				if (Response.Status == EStatus::Ok)
				{
					TRACE_COUNTER_INCREMENT(HttpDDC_GetHit);
					UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache hit for %ls from '%ls'",
						*NodeName, *WriteToString<96>(Response.Key), *Response.Name);
				}

				SharedOnComplete.Get()(MoveTemp(Response));
			});
		}
	}
}

void FHttpCacheStore::GetChunks(
	const TConstArrayView<FCacheGetChunkRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetChunkComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_GetChunks);
	TRACE_COUNTER_ADD(HttpDDC_Get, Requests.Num());

	if (Requests.IsEmpty())
	{
		return;
	}

	const auto HasSkipData = [](const FCacheGetChunkRequest& Request) { return EnumHasAnyFlags(Request.Policy, ECachePolicy::SkipData); };

	TArray<FCacheGetChunkRequest, TInlineAllocator<16>> ExistRequests;
	for (const FCacheGetChunkRequest& Request : Requests)
	{
		if (HasSkipData(Request))
		{
			ExistRequests.Emplace(Request);
		}
	}

	FRequestBarrier Barrier(Owner);
	TSharedRef<FOnCacheGetChunkComplete> SharedOnComplete = MakeShared<FOnCacheGetChunkComplete>(MoveTemp(OnComplete));
	if (!ExistRequests.IsEmpty())
	{
		TRefCountPtr<FExistsChunkBatchOp> Op = FExistsChunkBatchOp::New(*this, Owner);
		Op->Exists(ExistRequests, [this, SharedOnComplete](FCacheGetChunkResponse&& Response)
		{
			if (Response.Status == EStatus::Ok)
			{
				TRACE_COUNTER_INCREMENT(HttpDDC_GetHit);
				UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Cache hit for %ls from '%ls'",
					*NodeName, *WriteToString<96>(Response.Key), *Response.Name);
			}
			SharedOnComplete.Get()(MoveTemp(Response));
		});
	}

	// Only do the following if there are any non-existence requests
	if (ExistRequests.Num() != Requests.Num())
	{
		// TODO: This is inefficient because Jupiter doesn't allow us to get only part of a compressed blob, so we have to
		//		 get the whole thing and then decompress only the portion we need.  Furthermore, because there is no propagation
		//		 between cache stores during chunk requests, the fetched result won't end up in the local store.
		//		 These efficiency issues will be addressed by changes to the Hierarchy that translate chunk requests that
		//		 are missing in local/fast stores and have to be retrieved from slow stores into record requests instead.  That
		//		 will make this code path unused/uncommon as Jupiter will most always be a slow store with a local/fast store in front of it.
		//		 Regardless, to adhere to the functional contract, this implementation must exist.
		TArray<FCacheGetChunkRequest, TInlineAllocator<16>> SortedRequests;
		SortedRequests.Reserve(Requests.Num() - ExistRequests.Num());
		for (const FCacheGetChunkRequest& Request : Requests)
		{
			if (!HasSkipData(Request))
			{
				SortedRequests.Emplace(Request);
			}
		}
		SortedRequests.StableSort(TChunkLess());

		const FCacheGetChunkRequest* PendingGroupStartRequest = &SortedRequests[0];

		for (const FCacheGetChunkRequest& Request : SortedRequests)
		{
			const bool bMatchesExistingGroup = PendingGroupStartRequest != nullptr && PendingGroupStartRequest->Key == Request.Key && PendingGroupStartRequest->Id.IsValid() == Request.Id.IsValid();
			if (!bMatchesExistingGroup)
			{
				GetChunkGroupAsync(Owner, PendingGroupStartRequest, &Request, SharedOnComplete);
				PendingGroupStartRequest = &Request;
			}
		}
		GetChunkGroupAsync(Owner, PendingGroupStartRequest, SortedRequests.GetData() + SortedRequests.Num(), SharedOnComplete);
	}
}

static void RegisterInheritedCommandLineArg(const FStringView ArgName)
{
	FCommandLine::RegisterArgument(ArgName, ECommandLineArgumentFlags::EditorContext | ECommandLineArgumentFlags::CommandletContext | ECommandLineArgumentFlags::Inherit);
}

FString ParseConfigParamWithOverrides(const TCHAR* NodeName, const TCHAR* Config, const TCHAR* ParamName, FStringView ExistingValue, bool bIsSecret)
{
	FString OutValue(ExistingValue);
	FString OverrideName;
	FParse::Value(Config, *WriteToString<32>(ParamName, TEXT("=")), OutValue);
	if (FParse::Value(Config, *WriteToString<32>(TEXT("Env"), ParamName, TEXT("Override=")), OverrideName))
	{
		FString ParamEnv = FPlatformMisc::GetEnvironmentVariable(*OverrideName);
		if (!ParamEnv.IsEmpty())
		{
			OutValue = ParamEnv;
			UE_LOGF(LogDerivedDataCache, Log, "%ls: Found environment override for %ls %ls=%ls", NodeName, ParamName, *OverrideName, bIsSecret ? TEXT("{SECRET}") : *ParamEnv);
		}
	}
	if (FParse::Value(Config, *WriteToString<32>(TEXT("CommandLine"), ParamName, TEXT("Override=")), OverrideName))
	{
		if (FParse::Value(FCommandLine::Get(), *WriteToString<32>(OverrideName, TEXT("=")), OutValue))
		{
			RegisterInheritedCommandLineArg(OverrideName);
			UE_LOGF(LogDerivedDataCache, Log, "%ls: Found command line override for %ls %ls=%ls", NodeName, ParamName, *OverrideName, bIsSecret ? TEXT("{SECRET}") : *OutValue);
		}
	}
	return OutValue;
}

void FHttpCacheStoreParams::Parse(const TCHAR* NodeName, const TCHAR* Config)
{
	Name = NodeName;

	FString ServerId;
	if (FParse::Value(Config, TEXT("ServerID="), ServerId))
	{
		FString ServerEntry;
		const TCHAR* ServerSection = TEXT("StorageServers");
		const TCHAR* FallbackServerSection = TEXT("HordeStorageServers");
		if (GConfig->GetString(ServerSection, *ServerId, ServerEntry, GEngineIni))
		{
			Parse(NodeName, *ServerEntry);
		}
		else if (GConfig->GetString(FallbackServerSection, *ServerId, ServerEntry, GEngineIni))
		{
			Parse(NodeName, *ServerEntry);
		}
		else
		{
			UE_LOGF(LogDerivedDataCache, Warning, "%ls: Using ServerID=%ls which was not found in [%ls]", NodeName, *ServerId, ServerSection);
		}
	}

	FString OverrideName;

	// Host Params
	Host = ParseConfigParamWithOverrides(NodeName, Config, TEXT("Host"), Host, /*bIsSecret*/false);

	FParse::Value(Config, TEXT("DiscoveryHost="), DiscoveryHost);

	FParse::Value(Config, TEXT("HostPinnedPublicKeys="), HostPinnedPublicKeys);

	FParse::Bool(Config, TEXT("ResolveHostCanonicalName="), bResolveHostCanonicalName);

	// Http version Params
	HttpVersion = ParseConfigParamWithOverrides(NodeName, Config, TEXT("HttpVersion"), HttpVersion, /*bIsSecret*/false);

	// Unix Socket Params
	UnixSocketPath = ParseConfigParamWithOverrides(NodeName, Config, TEXT("UnixSocketPath"), UnixSocketPath, /*bIsSecret*/false);

	// Namespace Params

	if (Namespace.IsEmpty())
	{
		FParse::Value(Config, TEXT("Namespace="), Namespace);
	}
	FParse::Value(Config, TEXT("StructuredNamespace="), Namespace);

	// OAuth Params
	OAuthProvider = ParseConfigParamWithOverrides(NodeName, Config, TEXT("OAuthProvider"), OAuthProvider, /*bIsSecret*/false);

	FParse::Value(Config, TEXT("OAuthClientId="), OAuthClientId);

	OAuthSecret = ParseConfigParamWithOverrides(NodeName, Config, TEXT("OAuthSecret"), OAuthSecret, /*bIsSecret*/true);

	// If the secret is a file path, read the secret from the file.
	if (OAuthSecret.StartsWith(TEXT("file://")))
	{
		TStringBuilder<256> FilePath;
		FilePath << MakeStringView(OAuthSecret).RightChop(TEXTVIEW("file://").Len());
		if (!FFileHelper::LoadFileToString(OAuthSecret, *FilePath))
		{
			OAuthSecret.Empty();
			UE_LOGF(LogDerivedDataCache, Warning, "%ls: Failed to read OAuth secret file: %ls", NodeName, *FilePath);
		}
	}

	FParse::Value(Config, TEXT("OAuthScope="), OAuthScope);

	// OAuth Provider Identifier
	OAuthProviderIdentifier = ParseConfigParamWithOverrides(NodeName, Config, TEXT("OAuthProviderIdentifier"), OAuthProviderIdentifier, /*bIsSecret*/false);

	// OAuth Access
	// TODO-BEGIN: Replace this with:
	// OAuthAccessToken = ParseConfigParamWithOverrides(NodeName, Config, TEXT("OAuthAccessToken"), OAuthAccessToken, /*bIsSecret*/true);
	if (!FParse::Value(Config, TEXT("OAuthAccessToken="), OAuthAccessToken))
	{
		if (FParse::Value(Config, TEXT("OAuthAccess="), OAuthAccessToken))
		{
			UE_LOGF(LogDerivedDataCache, Warning, "%ls: Deprecated parameter 'OAuthAccess' found.  Please use 'OAuthAccessToken' instead.", NodeName);
		}
	}
	if (FParse::Value(Config, TEXT("EnvOAuthAccessTokenOverride="), OverrideName))
	{
		FString AccessToken = FPlatformMisc::GetEnvironmentVariable(*OverrideName);
		if (!AccessToken.IsEmpty())
		{
			OAuthAccessToken = AccessToken;
			// We do not log the access token as it is sensitive information.
			UE_LOGF(LogDerivedDataCache, Log, "%ls: Found environment override for OAuthAccess %ls={SECRET}", NodeName, *OverrideName);
		}
	}
	else if (FParse::Value(Config, TEXT("OAuthAccessTokenEnvOverride="), OverrideName))
	{
		UE_LOGF(LogDerivedDataCache, Warning, "%ls: Deprecated parameter 'OAuthAccessTokenEnvOverride' found.  Please use 'EnvOAuthAccessTokenOverride' instead.", NodeName);
		FString AccessToken = FPlatformMisc::GetEnvironmentVariable(*OverrideName);
		if (!AccessToken.IsEmpty())
		{
			OAuthAccessToken = AccessToken;
			// We do not log the access token as it is sensitive information.
			UE_LOGF(LogDerivedDataCache, Log, "%ls: Found OAuth access token in %ls.", NodeName, *OverrideName);
		}
	}
	if (FParse::Value(Config, TEXT("CommandLineOAuthAccessTokenOverride="), OverrideName))
	{
		if (FParse::Value(FCommandLine::Get(), *WriteToString<32>(OverrideName, TEXT("=")), OAuthAccessToken))
		{
			RegisterInheritedCommandLineArg(OverrideName);
			UE_LOGF(LogDerivedDataCache, Log, "%ls: Found command line override for OAuthAccess %ls={SECRET}", NodeName, *OverrideName);
		}
	}
	// TODO-END

	FParse::Value(Config, TEXT("AuthScheme="), AuthScheme);
	if (AuthScheme.IsEmpty())
	{
		AuthScheme = "Bearer";
	}

	FParse::Value(Config, TEXT("OAuthPinnedPublicKeys="), OAuthPinnedPublicKeys);

	// Cache Params
	LexFromString(bReadOnly, ParseConfigParamWithOverrides(NodeName, Config, TEXT("ReadOnly"), ::LexToString(bReadOnly), /*bIsSecret*/false));
	LexFromString(bWriteOnly, ParseConfigParamWithOverrides(NodeName, Config, TEXT("WriteOnly"), ::LexToString(bWriteOnly), /*bIsSecret*/false));

	if (bReadOnly && bWriteOnly)
	{
		UE_LOGF(LogDerivedDataCache, Display, "%ls: Both ReadOnly and WriteOnly specified.  This cache store will be inactive.", NodeName);
	}

	FParse::Bool(Config, TEXT("BypassProxy="), bBypassProxy);
}

ILegacyCacheStore* CreateHttpCacheStore(const TCHAR* NodeName, const TCHAR* Config, ICacheStoreOwner& Owner)
{
	FHttpCacheStoreParams Params;
	Params.Parse(NodeName, Config);

	bool bValidParams = true;

	if (Params.Host.IsEmpty())
	{
		UE_LOGF(LogDerivedDataCache, Error, "%ls: Missing required parameter 'Host'", NodeName);
		bValidParams = false;
	}
	else if (Params.Host == TEXTVIEW("None"))
	{
		UE_LOGF(LogDerivedDataCache, Log, "%ls: Disabled because Host is set to 'None'", NodeName);
		bValidParams = false;
	}

	if (Params.Namespace.IsEmpty())
	{
		Params.Namespace = FApp::GetProjectName();
		UE_LOGF(LogDerivedDataCache, Warning, "%ls: Missing required parameter 'StructuredNamespace', falling back to '%ls'", NodeName, *Params.Namespace);
	}

	if (bValidParams && (!Params.Host.StartsWith(TEXT("http://localhost")) || !Params.Host.StartsWith(TEXT("https://localhost"))))
	{
		bool bValidOAuthAccessToken = !Params.OAuthAccessToken.IsEmpty();

		bool bValidOAuthProviderIdentifier = !Params.OAuthProviderIdentifier.IsEmpty();

		bool bValidOAuthProvider = !Params.OAuthProvider.IsEmpty();
		if (bValidOAuthProvider)
		{
			if (!Params.OAuthProvider.StartsWith(TEXT("http://")) &&
				!Params.OAuthProvider.StartsWith(TEXT("https://")))
			{
				UE_LOGF(LogDerivedDataCache, Error, "%ls: OAuth provider '%ls' must be a complete URI including the scheme.", NodeName, *Params.OAuthProvider);
				bValidParams = false;
			}

			// No need for OAuthClientId and OAuthSecret if using a local provider.
			if (!Params.OAuthProvider.StartsWith(TEXT("http://localhost")))
			{
				if (Params.OAuthClientId.IsEmpty())
				{
					UE_LOGF(LogDerivedDataCache, Error, "%ls: Missing required parameter 'OAuthClientId'", NodeName);
					bValidOAuthProvider = false;
					bValidParams = false;
				}

				if (Params.OAuthSecret.IsEmpty())
				{
					UE_CLOGF(!bValidOAuthAccessToken && !bValidOAuthProviderIdentifier,
						LogDerivedDataCache, Error, "%ls: Missing required parameter 'OAuthSecret'", NodeName);
					bValidOAuthProvider = false;
				}
			}
		}

		if (!bValidOAuthAccessToken && !bValidOAuthProviderIdentifier && !bValidOAuthProvider)
		{
			UE_LOGF(LogDerivedDataCache, Error, "%ls: At least one OAuth configuration must be provided and valid. "
				"Options are 'OAuthProvider', 'OAuthProviderIdentifier', and 'OAuthAccessTokenEnvOverride'", NodeName);
			bValidParams = false;
		}
	}

	if (Params.OAuthScope.IsEmpty())
	{
		Params.OAuthScope = TEXTVIEW("cache_access");
	}

	if (bValidParams)
	{
		if (TUniquePtr<FHttpCacheStore> Store = MakeUnique<FHttpCacheStore>(Params, Owner); Store->IsUsable())
		{
			return Store.Release();
		}
		UE_LOGF(LogDerivedDataCache, Warning, "%ls: Failed to contact the service (%ls), will not use it.", NodeName, *Params.Host);
	}

	return nullptr;
}

ILegacyCacheStore* GetAnyHttpCacheStore(
	FString& OutDomain,
	FString& OutAccessToken,
	FString& OutNamespace)
{
	if (FHttpCacheStore* HttpBackend = FHttpCacheStore::GetAny())
	{
		OutDomain = HttpBackend->GetDomain();
		OutAccessToken = HttpBackend->GetAccessToken();
		OutNamespace = HttpBackend->GetNamespace();
		return HttpBackend;
	}
	return nullptr;
}

} // UE::DerivedData
