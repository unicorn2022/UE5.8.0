// Copyright Epic Games, Inc. All Rights Reserved.
#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HttpConstants.h"
#include "HttpManager.h"
#include "HttpRetrySystem.h"
#include "Http.h"
#include "HttpPath.h"
#include "HttpRouteHandle.h"
#include "IHttpRouter.h"
#include "HttpServerModule.h"
#include "Logging/LogScopedVerbosityOverride.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/QueuedThreadPool.h"
#include "Misc/ScopeExit.h"
#include "TestHarness.h"
#include "TestMacros/Assertions.h"
#include "Serialization/JsonSerializerMacros.h"
#include "catch2/generators/catch_generators.hpp"
#include "Ssl.h"
#include "WebTestFixtures.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

/**
 *  HTTP Tests
 *  -----------------------------------------------------------------------------------------------
 *
 *  PURPOSE:
 *
 *	Integration Tests to make sure all kinds of HTTP client features in C++ work well on different platforms,
 *  including but not limited to error handing, retrying, threading, streaming, SSL and profiling.
 *
 *  Refer to WebTests/README.md for more info about how to run these tests
 * 
 *  -----------------------------------------------------------------------------------------------
 */

#define HTTP_TAG "[HTTP]"
#define HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST 0.5f
 // Use a big chunk size so it triggers data received callback in time on all platforms
 // For xCurl, this is the minimum chunk size to trigger data received callback immediately, without waiting additional time for the recv buffer to fill up
#define HTTP_TEST_TIMEOUT_CHUNK_SIZE 32*1024

extern TAutoConsoleVariable<int32> CVarHttpMaxConcurrentRequests;
extern TAutoConsoleVariable<int32> CVarHttpEventLoopEnableChance;
extern TAutoConsoleVariable<FString> CVarHttpUrlPatternsToMockResponse;
extern TAutoConsoleVariable<FString> CVarHttpUrlPatternsToDisableFailedLog;
extern TAutoConsoleVariable<FString> CVarHttpUrlPatternsToInspect;
extern TAutoConsoleVariable<bool> CVarHttpLogJsonContentOnly;
extern TAutoConsoleVariable<bool> CVarHttpCancelSupportDuringBackOffEnabled;
extern TAutoConsoleVariable<int32> CVarHttpDurationInQueueToWarnInSeconds;

#if UE_HTTP_SUPPORT_OPTION_REUSE_CONNECTION
extern TAutoConsoleVariable<bool> CVarHttpReuseConnectionEnabled;
#endif

class FMockHttpModule : public FHttpModule
{
public:
	using FHttpModule::HttpConnectionTimeout;
	using FHttpModule::HttpTotalTimeout;
	using FHttpModule::HttpActivityTimeout;
};

namespace HttpTestUtils
{
	// checker.
	static void CheckContent(int DataLength, int RepeatAt, const TArray<uint8>& Content)
	{
		CHECK(Content.Num() == DataLength);

		bool bAllMatch = true;

		// Make sure the data got read is in good state
		for (int32 i = 0; i < Content.Num(); ++i)
		{
			bAllMatch &= (Content[i] == '0' + (i % RepeatAt));
		}

		CHECK(bAllMatch);
	};
};

class FHttpTestLogLevelInitializer
{
public:
	FHttpTestLogLevelInitializer(bool bInVeryVerbose)
		: OldVerbosity(LogHttp.GetVerbosity())
		, bVeryVerbose(bInVeryVerbose)
	{
		if (bVeryVerbose)
		{
			LogHttp.SetVerbosity(ELogVerbosity::VeryVerbose);
			LogSsl.SetVerbosity(ELogVerbosity::VeryVerbose);
		}
	}

	~FHttpTestLogLevelInitializer()
	{
		ResumeLogVerbosity();
	}

	void DisableWarningsInThisTest()
	{
		if (!bVeryVerbose)
		{
			LogHttp.SetVerbosity(ELogVerbosity::Error);
			LogSsl.SetVerbosity(ELogVerbosity::Error);
		}
	}

	void ResumeLogVerbosity()
	{
		if (OldVerbosity != LogHttp.GetVerbosity())
		{
			LogHttp.SetVerbosity(OldVerbosity);
			LogSsl.SetVerbosity(OldVerbosity);
		}
	}

	ELogVerbosity::Type OldVerbosity;
	bool bVeryVerbose = false;
};

class FMockRetryManager : public FHttpRetrySystem::FManager
{
public:
	using FHttpRetrySystem::FManager::FManager;
	using FHttpRetrySystem::FManager::RequestList;
	using FHttpRetrySystem::FManager::FHttpRetryRequestEntry;
	using FHttpRetrySystem::FManager::RetryTimeoutRelativeSecondsDefault;
	using FHttpRetrySystem::FManager::RetryLimitCountDefault;
	using FHttpRetrySystem::FManager::RetryLimitCountForConnectionErrorDefault;

	bool IsEmpty()
	{
		FScopeLock ScopeLock(&RequestListLock);
		return RequestList.IsEmpty();
	}
};

class FHttpModuleTestFixture
{
public:
	FHttpModuleTestFixture()
		: bRunHeavyTests(false)
		, bRetryEnabled(true)
		, bEventLoopEnabled(true)
	{
		FParse::Bool(FCommandLine::Get(), TEXT("run_heavy_tests="), bRunHeavyTests);
		FParse::Bool(FCommandLine::Get(), TEXT("retry_enabled="), bRetryEnabled);
		FParse::Bool(FCommandLine::Get(), TEXT("eventloop_enabled="), bEventLoopEnabled);
		FParse::Value(FCommandLine::Get(), TEXT("web_server_unix_socket="), WebServerUnixSocket);

		CVarHttpEventLoopEnableChance->Set(bEventLoopEnabled ? 100 : 0);

		InitModule();

		CVarHttpInsecureProtocolEnabled->Set(true);
	}

	void InitModule()
	{
		HttpModule = new FMockHttpModule();
		HttpModule->StartupModule();
		if (bRetryEnabled)
		{
			HttpRetryManager = MakeShared<FMockRetryManager>(FHttpRetrySystem::FRetryLimitCountSetting(0), FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting(/*RetryTimeoutRelativeSeconds*/));
		}
	}

	void ShutdownModule()
	{
		HttpRetryManager = nullptr;

		if (HttpModule)
		{
			HttpModule->ShutdownModule();
			delete HttpModule;
			HttpModule = nullptr;
		}
	}

	virtual ~FHttpModuleTestFixture()
	{
		ShutdownModule();
	}

	void DisableWarningsInThisTest()
	{
		HttpTestLogLevelInitializer.DisableWarningsInThisTest();
	}

	void ResumeLogVerbosity()
	{
		HttpTestLogLevelInitializer.ResumeLogVerbosity();
	}

	TSharedRef<IHttpRequest> CreateRequest()
	{
		return bRetryEnabled ? HttpRetryManager->CreateRequest() : HttpModule->CreateRequest();
	}

	void SetInvalidDomainToTestConnectTimeout() { Settings.WebServerIp = TEXT("10.255.255.1"); Settings.WebServerHttpPort = 8765; } // non-routable IP address with a random port
	const FString UrlDomain() const { return FString::Format(TEXT("{0}:{1}"), { *Settings.WebServerIp, Settings.WebServerHttpPort }); }
	const FString UrlHttpTests() const { return Settings.UrlHttpTests(); }
	const FString UrlToTestMethods() const { return FString::Format(TEXT("{0}/methods"), { *UrlHttpTests() }); }
	const FString UrlStreamDownload(uint32 Chunks, uint32 ChunkSize, uint32 ChunkLatency=0) { return FString::Format(TEXT("{0}/streaming_download/{1}/{2}/{3}/"), { *UrlHttpTests(), Chunks, ChunkSize, ChunkLatency }); }
	const FString UrlStreamUpload() { return FString::Format(TEXT("{0}/streaming_upload_put"), { *UrlHttpTests() }); }
	const FString UrlMockLatency(uint32 Latency) const { return FString::Format(TEXT("{0}/mock_latency/{1}/"), { *UrlHttpTests(), Latency }); }
	const FString UrlMockStatus(uint32 StatusCode) const { return FString::Format(TEXT("{0}/mock_status/{1}/"), { *UrlHttpTests(), StatusCode }); }
	const FString UrlUnixSocketHttpTests() const { return "http://localhost/webtests/unixsockettests"; }
	const FString UrlPostEchoBody() const { return FString::Format(TEXT("{0}/post_echo_body"), { *UrlHttpTests() }); }

	FWebTestSettings Settings;
	FString WebServerUnixSocket;
	FMockHttpModule* HttpModule = nullptr;
	bool bRunHeavyTests;
	bool bRetryEnabled;
	bool bEventLoopEnabled;
	FHttpTestLogLevelInitializer HttpTestLogLevelInitializer { Settings.bVeryVerbose };
	TSharedPtr<FMockRetryManager> HttpRetryManager;
};

TEST_CASE_METHOD(FHttpModuleTestFixture, "Shutdown http module without issue when there are ongoing upload http requests.", HTTP_TAG)
{
	DisableWarningsInThisTest(); // for: [FHttpManager::Shutdown] Unbinding delegates for 5 outstanding Http Requests

	uint32 ChunkSize = 1024 * 1024;
	TArray<uint8> DataChunk;
	DataChunk.SetNum(ChunkSize);
	FMemory::Memset(DataChunk.GetData(), 'd', ChunkSize);

	for (int32 i = 0; i < 10; ++i)
	{
		IHttpRequest* LeakingHttpRequest = FPlatformHttp::ConstructRequest(); // Leaking in purpose to make sure it's ok

		TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
		HttpRequest->SetURL(UrlToTestMethods());
		HttpRequest->SetVerb(TEXT("PUT"));
		// TODO: Use some shared data, like cookie, openssl session etc.
		HttpRequest->SetContent(DataChunk);
		HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
		});
		HttpRequest->ProcessRequest();
	}

	HttpModule->GetHttpManager().Tick(0.0f);
}

TEST_CASE("Get domain of url can work well", HTTP_TAG)
{
	FString DefaultUrlDomain = FPlatformHttp::GetUrlDomain(TEXT("https://www.epicgames.com/the/relative/path/"));
	CHECK(DefaultUrlDomain == TEXT("www.epicgames.com"));
}

TEST_CASE_METHOD(FHttpModuleTestFixture, "Duplicated scheme protocol will trigger warning", HTTP_TAG)
{
	// TODO: find a way to catch expected warning log instead
	DisableWarningsInThisTest();

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	CHECK(HttpRequest->GetVerb() == TEXT("GET"));

	HttpRequest->SetURL(TEXT("https://https://www.epicgames.com"));
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FHttpModuleTestFixture, "Shutdown http module without issue when there are ongoing streaming http requests with timeout.", HTTP_TAG)
{
	if (!bRunHeavyTests)
	{
		return;
	}

	// When use generator, it doesn't do the ctor and dtor of FHttpModuleTestFixture each time, so manually 
	// shutdown and init here to shutdown module a lot of times
	ShutdownModule();
	InitModule();

	int32 NumRequests = GENERATE(
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
		11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
		21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
		31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
		41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
		51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
		61, 62, 63, 64, 65, 66, 67, 68, 69, 70,
		71, 72, 73, 74, 75, 76, 77, 78, 79, 80,
		81, 82, 83, 84, 85, 86, 87, 88, 89, 90,
		91, 92, 93, 94, 95, 96, 97, 98, 99, 100
	);

	//Output NumRequests when error occurs.
	UNSCOPED_INFO(NumRequests);
	HttpModule->HttpTotalTimeout = 2.0f;
	HttpModule->HttpActivityTimeout = 1.0f;

	DYNAMIC_SECTION(" making " << NumRequests << " requests")
	{
		DisableWarningsInThisTest();

		uint32 ChunkSize = 1024 * 1024;
		TArray<uint8> DataChunk;
		DataChunk.SetNum(ChunkSize);
		FMemory::Memset(DataChunk.GetData(), 'd', ChunkSize);

		for (int32 i = 0; i < NumRequests; ++i)
		{
			{
				TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
				HttpRequest->SetURL(UrlToTestMethods());
				HttpRequest->SetVerb(TEXT("PUT"));
				HttpRequest->SetContent(DataChunk);
				HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
					CHECK(bSucceeded);
					});
				HttpRequest->ProcessRequest();
			}

			{
				TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
				HttpRequest->SetURL(UrlStreamDownload(2, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 2));
				HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
					CHECK(bSucceeded);
				});
				HttpRequest->ProcessRequest();
			}
		}

		HttpModule->GetHttpManager().Tick(0.0f);
	}

	ShutdownModule();
}

class FWaitUntilCompleteHttpFixture : public FHttpModuleTestFixture
{
public:
	FWaitUntilCompleteHttpFixture()
	{
		HttpModule->GetHttpManager().SetRequestAddedDelegate(FHttpManagerRequestAddedDelegate::CreateRaw(this, &FWaitUntilCompleteHttpFixture::OnRequestAdded));
		HttpModule->GetHttpManager().SetRequestCompletedDelegate(FHttpManagerRequestCompletedDelegate::CreateRaw(this, &FWaitUntilCompleteHttpFixture::OnRequestCompleted));
	}

	~FWaitUntilCompleteHttpFixture()
	{
		WaitUntilAllHttpRequestsComplete();

		CHECK(ExpectingExtraCallbacks == 0);

		HttpModule->GetHttpManager().SetRequestAddedDelegate(FHttpManagerRequestAddedDelegate());
		HttpModule->GetHttpManager().SetRequestCompletedDelegate(FHttpManagerRequestCompletedDelegate());
	}

	void OnRequestAdded(const FHttpRequestRef& Request)
	{
		++OngoingRequests;
	}

	void OnRequestCompleted(const FHttpRequestRef& Request)
	{
		ensure(--OngoingRequests >= 0);
	}

	void TickHttpManager()
	{
		double Now = FPlatformTime::Seconds();
		static double LastTick = Now;
		double Duration = Now - LastTick;
		LastTick = Now;
		HttpModule->GetHttpManager().Tick(Duration);
		FPlatformProcess::Sleep(TickFrequency);
	}

	void WaitUntilAllHttpRequestsComplete()
	{
		while (HasOngoingRequest())
		{
			TickHttpManager();
		}

		// In case in http thread the http request complete and set OngoingRequests to 0, http manager never 
		// had chance to Tick and remove the request
		TickHttpManager();
	}

	bool HasOngoingRequest() const
	{
		return OngoingRequests != 0 || (bRetryEnabled && !HttpRetryManager->IsEmpty());
	}

	std::atomic<int32> OngoingRequests = 0;
	float TickFrequency = 1.0f / 60; /*60 FPS*/;

	uint32 RetryLimitCount = 0;
	uint32 ExpectingExtraCallbacks = 0;
};

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http method use Get by default", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	CHECK(HttpRequest->GetVerb() == TEXT("GET"));
	HttpRequest->SetURL(UrlToTestMethods());
	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http methods without payloads", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlToTestMethods());

#if UE_HTTP_SUPPORT_VERB_CONNECT
	const ANSICHAR* Method = GENERATE("GET", "DELETE", "HEAD", "OPTIONS", "TRACE", "CONNECT");
#else // Can't use macro UE_HTTP_SUPPORT_VERB_CONNECT inside GENERATE macro, so define it here again
	const ANSICHAR* Method = GENERATE("GET", "DELETE", "HEAD", "OPTIONS", "TRACE");
#endif

	const TCHAR* Version = GENERATE(FHttpConstants::VERSION_2TLS, FHttpConstants::VERSION_1_1);

	DYNAMIC_SECTION("Test methods with " << Method << " request on version " << Version)
	{
		FString MethodStr = FString(Method);
		HttpRequest->SetVerb(MethodStr);
		HttpRequest->SetOption(HttpRequestOptions::HttpVersion, Version);
	}

	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http methods with payloads", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlToTestMethods());

	const ANSICHAR* Method = GENERATE("POST", "PUT", "PATCH");
	const TCHAR* Version = GENERATE(FHttpConstants::VERSION_2TLS, FHttpConstants::VERSION_1_1);

	DYNAMIC_SECTION("Test methods with " << Method << " request on version " << Version)
	{
		FString MethodStr = FString(Method);
		HttpRequest->SetVerb(MethodStr);
		HttpRequest->SetOption(HttpRequestOptions::HttpVersion, Version);
	}

	uint32 ChunkSize = 1024 * 1024;
	TArray<uint8> DataChunk;
	DataChunk.SetNum(ChunkSize);
	FMemory::Memset(DataChunk.GetData(), 'd', ChunkSize);
	HttpRequest->SetContent(MoveTemp(DataChunk));

	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http request can LogContent correctly with different strings", HTTP_TAG)
{
	DisableWarningsInThisTest();

	CVarHttpUrlPatternsToInspect->Set(TEXT("methods"));
	CVarHttpLogJsonContentOnly->Set(false);

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlToTestMethods());
	HttpRequest->SetVerb(FHttpConstants::VERB_POST);

	const TCHAR* Content = GENERATE(
		TEXT(""),
		TEXT("\n"),
		TEXT("\n\n"),
		TEXT("\r"),
		TEXT("\r\r"),
		TEXT("\n\r"),
		TEXT("\r\n"),
		TEXT("l"),
		TEXT("li"),
		TEXT("line"),
		TEXT("line1\r\nl"),
		TEXT("line1\n\rline2"),
		TEXT("line1\r\nline2"),
		TEXT("line1\r\nline2\r"),
		TEXT("line1\r\nline2\n"),
		TEXT("line1\r\nline2\r\n"),
		TEXT("line1\r\nline2\n\r")
	);
	HttpRequest->SetContentAsString(Content);
	HttpRequest->SetHeader(FHttpConstants::HEADER_CONTENT_TYPE, FHttpConstants::MEDIATYPE_JSON);

	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		// TODO: Find a way to validate output logs

		CVarHttpUrlPatternsToInspect->Set(TEXT(""));
		CVarHttpLogJsonContentOnly->Set(true);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http methods which support to upload payload can work well without any payload", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlToTestMethods());

	const ANSICHAR* Method = GENERATE("POST", "PUT", "PATCH");
	const TCHAR* Version = GENERATE(FHttpConstants::VERSION_2TLS, FHttpConstants::VERSION_1_1);

	DYNAMIC_SECTION("Test methods with " << Method << " request on version " << Version)
	{
		FString MethodStr = FString(Method);
		HttpRequest->SetVerb(MethodStr);
		HttpRequest->SetOption(HttpRequestOptions::HttpVersion, Version);
	}

	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can process https request", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetURL(TEXT("https://www.unrealengine.com/"));
	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can process multiple https requests", HTTP_TAG)
{
	// Suppress only "waited in queue" warnings by setting the threshold to an impossibly high value
	// This allows other LogHttp warnings to still be visible
	int32 OldQueueWarnThreshold = CVarHttpDurationInQueueToWarnInSeconds.GetValueOnAnyThread();
	CVarHttpDurationInQueueToWarnInSeconds->Set(MAX_int32);
	ON_SCOPE_EXIT { CVarHttpDurationInQueueToWarnInSeconds->Set(OldQueueWarnThreshold); };

	TArray<FString> Urls{
		TEXT("https://www.unrealengine.com/"),
		TEXT("https://www.epicgames.com/"),
		//TEXT("https://www.fortnite.com/"), // TODO: There is error "47 (Number of redirects hit maximum amount)" on xCurl, enable this once it's resolved
		TEXT("https://www.fallguys.com/"),
		TEXT("https://www.rocketleague.com/"),
		TEXT("https://www.fab.com/"),
		TEXT("https://www.google.com/"),
		TEXT("https://www.amazon.com/"),
		TEXT("https://www.microsoft.com/")
	};

	for (int i = 0; i < 3; ++i)
	{
		for (const FString& Url : Urls)
		{
			TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
			HttpRequest->SetVerb(TEXT("GET"));
			HttpRequest->SetURL(Url);

			//HttpRequest->SetOption(HttpRequestOptions::HttpVersion, FHttpConstants::VERSION_2TLS);

			HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
				CHECK(bSucceeded);
			});
			HttpRequest->ProcessRequest();
		}
	}

	for (const FString& Url : Urls)
	{
		for (int i = 0; i < 3; ++i)
		{
			TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
			HttpRequest->SetVerb(TEXT("GET"));
			HttpRequest->SetURL(Url);

			//HttpRequest->SetOption(HttpRequestOptions::HttpVersion, FHttpConstants::VERSION_2TLS);

			HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
				CHECK(bSucceeded);
			});
			HttpRequest->ProcessRequest();
		}
	}
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can mock connection error through CVar", HTTP_TAG)
{
	DisableWarningsInThisTest();

	CVarHttpUrlPatternsToMockResponse->Set(TEXT("epicgames.com->0 unrealengine.com->503"));

	float ExpectedTimeoutDuration = 2.0f;
	HttpModule->HttpConnectionTimeout = ExpectedTimeoutDuration;
	const double StartTime = FPlatformTime::Seconds();

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(TEXT("https://www.epicgames.com/"));
	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime, ExpectedTimeoutDuration](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(!bSucceeded);
		CHECK(!HttpResponse);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::ConnectionError);
		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, ExpectedTimeoutDuration, HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST));
		CVarHttpUrlPatternsToMockResponse->Set(TEXT(""));
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can mock response failure through CVar", HTTP_TAG)
{
	CVarHttpUrlPatternsToMockResponse->Set(TEXT("epicgames.com->0 unrealengine.com->503"));

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetURL(TEXT("https://www.unrealengine.com/"));
	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 503);
		CVarHttpUrlPatternsToMockResponse->Set(TEXT(""));
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can mock different responses for different verbs", HTTP_TAG)
{
	CVarHttpUrlPatternsToMockResponse->Set(TEXT("epicgames.com++GET->500 epicgames.com++POST->503"));

	TSharedRef<IHttpRequest> HttpRequestGet = CreateRequest();
	HttpRequestGet->SetVerb(FHttpConstants::VERB_GET);
	HttpRequestGet->SetURL(TEXT("https://www.epicgames.com/"));
	HttpRequestGet->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 500);
	});
	HttpRequestGet->ProcessRequest();

	TSharedRef<IHttpRequest> HttpRequestPost = CreateRequest();
	HttpRequestPost->SetVerb(FHttpConstants::VERB_POST);
	HttpRequestPost->SetURL(TEXT("https://www.epicgames.com/"));
	HttpRequestPost->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 503);
		CVarHttpUrlPatternsToMockResponse->Set(TEXT(""));
	});
	HttpRequestPost->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can mock successful response through CVar", HTTP_TAG)
{
	CVarHttpUrlPatternsToMockResponse->Set(TEXT("epicgames.com->200 unrealengine_wrong_domain.com->200++{\"var_str\":\"abcd\",\"var_int\":20}++Content-Type:application/json"));

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetVerb(FHttpConstants::VERB_GET);
	HttpRequest->SetURL(TEXT("https://www.unrealengine_wrong_domain.com/"));
	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);
		CHECK(HttpResponse->GetHeader(FHttpConstants::HEADER_CONTENT_TYPE) == FHttpConstants::MEDIATYPE_JSON);
		CHECK(HttpResponse->GetContentAsUtf8StringView() == "{\"var_str\":\"abcd\",\"var_int\":20}");
		CVarHttpUrlPatternsToMockResponse->Set(TEXT(""));
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can process request with gzip support", HTTP_TAG)
{
	uint32 DataLength = 1025;
	uint32 RepeatAt = 9;

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetURL(FString::Format(TEXT("{0}/get_data_gzip/{1}/{2}/"), { *UrlHttpTests(), DataLength, RepeatAt }));
	HttpRequest->SetHeader(TEXT("Accept-Encoding"), TEXT("gzip"));
	HttpRequest->OnProcessRequestComplete().BindLambda([DataLength, RepeatAt](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);
		
		// Using get response.
		HttpTestUtils::CheckContent(DataLength, RepeatAt, HttpResponse->GetContent());

		// The most we can test is that it return the same thing, since
		// implementations may not be able to actually do the move semantics. 
		TArray<uint8> TakenContent = HttpResponse->TakeContent();
		HttpTestUtils::CheckContent(DataLength, RepeatAt, TakenContent);
	});
	HttpRequest->ProcessRequest();
}

// Test the cases when http request wasn't added into http manager and completed, but user code reset the HttpRequest 
// in the complete callback, so it will be destroyed because that's the only reference. 
// Verify there is no more access from http code after the complete callback in such scenarios(FinishRequestNotInHttpManager)
class FEarlyCompleteHttpFixture : public FWaitUntilCompleteHttpFixture
{
public:
	~FEarlyCompleteHttpFixture()
	{
		while (HttpRequest.IsValid())
		{
			TickHttpManager();
		}
	}

	TSharedPtr<IHttpRequest> HttpRequest;
};

TEST_CASE_METHOD(FEarlyCompleteHttpFixture, "Can delete failed request during complete callback for mock failures", HTTP_TAG)
{
	// When use retry manager, the request adapter will keep the shared ptr of the request impl, which can't reproduce the deletion
	if (bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	CVarHttpUrlPatternsToMockResponse->Set(TEXT("epicgames.com->0 unrealengine.com->503"));

	HttpRequest = CreateRequest();

	SECTION("For mock failure with status code")
	{
		HttpRequest->SetURL(TEXT("https://www.unrealengine.com/"));
	}
	SECTION("For mock failure with connection error")
	{
		HttpModule->HttpConnectionTimeout = 2.0f;
		HttpRequest->SetURL(TEXT("https://www.epicgames.com/"));
	}

	HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr InHttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CVarHttpUrlPatternsToMockResponse->Set(TEXT(""));
		HttpRequest.Reset();
	});

	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FEarlyCompleteHttpFixture, "Can delete failed request during complete callback in pre process phase", HTTP_TAG)
{
	// When use retry manager, the request adapter will keep the shared ptr of the request impl, which can't reproduce the deletion
	if (bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	HttpRequest = CreateRequest();

	SECTION("For pre check failure when verb was set to empty")
	{
		HttpRequest->SetURL(TEXT("https://www.unrealengine.com/"));
		HttpRequest->SetVerb(TEXT(""));
	}
	SECTION("For setup failure")
	{
		FString Filename = FString(FPlatformProcess::UserSettingsDir()) / TEXT("TestStreamUploadNonExist.dat");
		HttpRequest->SetURL(UrlStreamUpload());
		HttpRequest->SetVerb(TEXT("PUT"));
		HttpRequest->SetHeader(TEXT("Content-Disposition"), TEXT("attachment;filename=TestStreamUploadNonExist.dat"));
		HttpRequest->SetContentAsStreamedFile(Filename);
	}

	HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr InHttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		HttpRequest.Reset();
	});

	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can complete successfully for different response codes", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetVerb(TEXT("GET"));

	int32 ExpectedStatusCode = 0;
	SECTION("For status 200")
	{
		ExpectedStatusCode = 200;
	}
	SECTION("For status 206")
	{
		ExpectedStatusCode = 206;
	}
	SECTION("For status 400")
	{
		ExpectedStatusCode = 400;
	}

	HttpRequest->SetURL(UrlMockStatus(ExpectedStatusCode));

	HttpRequest->OnProcessRequestComplete().BindLambda([ExpectedStatusCode](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == ExpectedStatusCode);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can do blocking call", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlToTestMethods());
	HttpRequest->ProcessRequestUntilComplete();
	CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Succeeded);
	FHttpResponsePtr HttpResponse = HttpRequest->GetResponse();
	REQUIRE(HttpResponse != nullptr);
	CHECK(HttpResponse->GetResponseCode() == 200);
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Get large response content without chunks", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	uint32 DataLength = 0;
	uint32 RepeatAt = 0;
	SECTION("case A")
	{
		DataLength = 1024 * 1024;
		RepeatAt = 10;
	}
	SECTION("cast B")
	{
		DataLength = 1025 * 1023;
		RepeatAt = 9;
	}
	HttpRequest->SetURL(FString::Format(TEXT("{0}/get_data_without_chunks/{1}/{2}/"), { *UrlHttpTests(), DataLength, RepeatAt}));
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->OnProcessRequestComplete().BindLambda([DataLength, RepeatAt](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);

		HttpTestUtils::CheckContent(DataLength, RepeatAt, HttpResponse->GetContent());
		TArray<uint8> TakenContent = HttpResponse->TakeContent();
		HttpTestUtils::CheckContent(DataLength, RepeatAt, TakenContent);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http request connect timeout", HTTP_TAG)
{
	DisableWarningsInThisTest();

	HttpModule->HttpActivityTimeout = 3.0f; // Make sure this won't be triggered before establishing connection
	float ExpectedTimeoutDuration = 15.0f;
	HttpModule->HttpConnectionTimeout = ExpectedTimeoutDuration;

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();

	SetInvalidDomainToTestConnectTimeout();
	HttpRequest->SetURL(UrlToTestMethods());
	HttpRequest->SetVerb(TEXT("GET"));

	const double StartTime = FPlatformTime::Seconds();

	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime, ExpectedTimeoutDuration](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(!bSucceeded);
		CHECK(HttpResponse == nullptr);
		CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::ConnectionError);
		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, ExpectedTimeoutDuration, UE_HTTP_CONNECTION_TIMEOUT_MAX_DEVIATION));
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Streaming http download", HTTP_TAG)
{
	uint32 Chunks = 3;
	uint32 ChunkSize = 1024*1024;

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamDownload(Chunks, ChunkSize));
	HttpRequest->SetVerb(TEXT("GET"));

	TSharedRef<int64> TotalBytesReceived = MakeShared<int64>(0);

	SECTION("Success without stream provided")
	{
		HttpRequest->OnProcessRequestComplete().BindLambda([Chunks, ChunkSize](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
			REQUIRE(HttpResponse != nullptr);
			CHECK(HttpResponse->GetResponseCode() == 200);
			CHECK(!HttpResponse->GetAllHeaders().IsEmpty());
			CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);
		});
	}
	SECTION("Success with customized stream")
	{
		class FTestHttpReceiveStream final : public FArchive
		{
		public:
			FTestHttpReceiveStream(TSharedRef<int64> InTotalBytesReceived)
				: TotalBytesReceived(InTotalBytesReceived)
			{
			}

			virtual void Serialize(void* V, int64 Length) override
			{
				*TotalBytesReceived += Length;
			}

			TSharedRef<int64> TotalBytesReceived;
		};

		TSharedRef<FTestHttpReceiveStream> Stream = MakeShared<FTestHttpReceiveStream>(TotalBytesReceived);
		CHECK(HttpRequest->SetResponseBodyReceiveStream(Stream));

		HttpRequest->OnProcessRequestComplete().BindLambda([Chunks, ChunkSize, TotalBytesReceived](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
			REQUIRE(HttpResponse != nullptr);
			CHECK(HttpResponse->GetResponseCode() == 200);
			CHECK(!HttpResponse->GetAllHeaders().IsEmpty());
			CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);
			CHECK(HttpResponse->GetContent().IsEmpty());
			CHECK(*TotalBytesReceived == Chunks * ChunkSize);
		});
	}
	SECTION("Success with customized stream delegate")
	{
		FHttpRequestStreamDelegateV2 Delegate;
		Delegate.BindLambda([TotalBytesReceived](void* Ptr, int64& Length) {
			*TotalBytesReceived += Length;
		});
		CHECK(HttpRequest->SetResponseBodyReceiveStreamDelegateV2(Delegate));

		HttpRequest->OnProcessRequestComplete().BindLambda([Chunks, ChunkSize, TotalBytesReceived](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
			REQUIRE(HttpResponse != nullptr);
			CHECK(HttpResponse->GetResponseCode() == 200);
			CHECK(!HttpResponse->GetAllHeaders().IsEmpty());
			CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);
			CHECK(HttpResponse->GetContent().IsEmpty());
			CHECK(*TotalBytesReceived == Chunks * ChunkSize);
		});
	}
	SECTION("Use customized stream to receive response body but failed when serialize")
	{
		DisableWarningsInThisTest();

		class FTestHttpReceiveStream final : public FArchive
		{
		public:
			FTestHttpReceiveStream(TSharedRef<int64> InTotalBytesReceived)
				: TotalBytesReceived(InTotalBytesReceived)
			{
			}

			virtual void Serialize(void* V, int64 Length) override
			{
				*TotalBytesReceived += Length;
				SetError();
			}

			TSharedRef<int64> TotalBytesReceived;
		};

		TSharedRef<FTestHttpReceiveStream> Stream = MakeShared<FTestHttpReceiveStream>(TotalBytesReceived);
		CHECK(HttpRequest->SetResponseBodyReceiveStream(Stream));

		HttpRequest->OnProcessRequestComplete().BindLambda([ChunkSize, TotalBytesReceived](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(!bSucceeded);
			CHECK(HttpResponse != nullptr);
			CHECK(*TotalBytesReceived <= ChunkSize);
		});
	}
	SECTION("Use customized stream delegate to receive response body but failed when call")
	{
		DisableWarningsInThisTest();

		FHttpRequestStreamDelegateV2 Delegate;
		Delegate.BindLambda([TotalBytesReceived](void* Ptr, int64& Length) {
			*TotalBytesReceived += Length;
			Length = 0; // Mark as no data was serialized successfully
		});
		CHECK(HttpRequest->SetResponseBodyReceiveStreamDelegateV2(Delegate));

		HttpRequest->OnProcessRequestComplete().BindLambda([ChunkSize, TotalBytesReceived](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(!bSucceeded);
			CHECK(HttpResponse != nullptr);
			CHECK(*TotalBytesReceived <= ChunkSize);
		});
	}
	SECTION("Success with file stream to receive response body")
	{
		FString Filename = FString(FPlatformProcess::UserSettingsDir()) / TEXT("TestStreamDownload.dat");
		FArchive* RawFile = IFileManager::Get().CreateFileWriter(*Filename);
		CHECK(RawFile != nullptr);
		TSharedRef<FArchive> FileToWrite = MakeShareable(RawFile);
		CHECK(HttpRequest->SetResponseBodyReceiveStream(FileToWrite));

		HttpRequest->OnProcessRequestComplete().BindLambda([Chunks, ChunkSize, Filename, FileToWrite](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
			REQUIRE(HttpResponse != nullptr);
			CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);
			CHECK(HttpResponse->GetContent().IsEmpty());
			CHECK(HttpResponse->GetResponseCode() == 200);
			CHECK(!HttpResponse->GetAllHeaders().IsEmpty());

			FileToWrite->FlushCache();
			FileToWrite->Close();

			TSharedRef<FArchive> FileToRead = MakeShareable(IFileManager::Get().CreateFileReader(*Filename));
			CHECK(FileToRead->TotalSize() == Chunks * ChunkSize);
			FileToRead->Close();

			IFileManager::Get().Delete(*Filename);
		});
	}

	HttpRequest->ProcessRequest();
}

// This user streaming class is supposed to be used to receive streaming data through function OnReceivedData 
// and it's not supposed to be called once destroyed
class FUserStreamingClass
{
public:
	FUserStreamingClass()
		: TotalBytesReceived(new int64(0))
	{
	}

	~FUserStreamingClass()
	{
		delete TotalBytesReceived;
		TotalBytesReceived = nullptr;
	}

	void OnReceivedData(void* Ptr, int64& Length)
	{
		*TotalBytesReceived += Length;
	}

	int64* TotalBytesReceived;
};

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "In streaming downloading http request won't trigger response body receive delegate after canceling", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamDownload(60, 1024*1024));

	TSharedPtr<FUserStreamingClass> UserInstance = MakeShared<FUserStreamingClass>();

	FHttpRequestStreamDelegateV2 Delegate;
	Delegate.BindThreadSafeSP(UserInstance.ToSharedRef(), &FUserStreamingClass::OnReceivedData);
	CHECK(HttpRequest->SetResponseBodyReceiveStreamDelegateV2(Delegate));

	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(!bSucceeded);
		CHECK(HttpResponse != nullptr);
	});
	HttpRequest->ProcessRequest();

	while (*UserInstance->TotalBytesReceived == 0) // Make sure it started receiving data
	{
		FPlatformProcess::Sleep(0.001f);
	}
	CHECK(*UserInstance->TotalBytesReceived < 60 * 1024 * 1024);
	HttpRequest->CancelRequest();
	CHECK(UserInstance.GetSharedReferenceCount() == 1);
	UserInstance.Reset();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "In streaming downloading http request won't crash if shared ptr bound to delegate got destroyed", HTTP_TAG)
{
	DisableWarningsInThisTest(); // Failed writing received data to disk/application

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamDownload(60, 1024*1024));

	TSharedPtr<FUserStreamingClass> UserInstance = MakeShared<FUserStreamingClass>();

	FHttpRequestStreamDelegateV2 Delegate;
	Delegate.BindThreadSafeSP(UserInstance.ToSharedRef(), &FUserStreamingClass::OnReceivedData);
	CHECK(HttpRequest->SetResponseBodyReceiveStreamDelegateV2(Delegate));
	HttpRequest->ProcessRequest();

	while (*UserInstance->TotalBytesReceived == 0) // Make sure it started receiving data
	{
		FPlatformProcess::Sleep(0.001f);
	}
	CHECK(*UserInstance->TotalBytesReceived < 60 * 1024 * 1024);
	CHECK(UserInstance.GetSharedReferenceCount() == 1);
	UserInstance.Reset();
}

class FInvalidateDelegateShutdownFixture : public FHttpModuleTestFixture
{
public:
	FInvalidateDelegateShutdownFixture()
	{
		UserStreamingInstance = MakeShared<FUserStreamingClass>();
	}

	TSharedPtr<FUserStreamingClass> UserStreamingInstance;
};

TEST_CASE_METHOD(FInvalidateDelegateShutdownFixture, "Shutdown http module without issue when there are ongoing download http requests", HTTP_TAG)
{
	DisableWarningsInThisTest();

	for (int32 i = 0; i < 10; ++i)
	{
		TSharedRef<IHttpRequest> HttpRequest = HttpModule->CreateRequest();
		HttpRequest->SetURL(UrlStreamDownload(10, 1024*1024));
		FHttpRequestStreamDelegateV2 Delegate;
		Delegate.BindThreadSafeSP(UserStreamingInstance.ToSharedRef(), &FUserStreamingClass::OnReceivedData);
		CHECK(HttpRequest->SetResponseBodyReceiveStreamDelegateV2(Delegate));

		HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
		});
		HttpRequest->ProcessRequest();
	}

	while (*UserStreamingInstance->TotalBytesReceived == 0) // Make sure it started receiving data
	{
		FPlatformProcess::Sleep(0.001f);
	}

	HttpModule->GetHttpManager().Tick(0.1f);
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can run parallel stream download requests", HTTP_TAG)
{
	uint32 Chunks = 5;
	uint32 ChunkSize = 1024*1024;

	for (int i = 0; i < 3; ++i)
	{
		TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
		HttpRequest->SetURL(UrlStreamDownload(Chunks, ChunkSize));
		HttpRequest->SetVerb(TEXT("GET"));
		HttpRequest->OnProcessRequestComplete().BindLambda([Chunks, ChunkSize](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);
			CHECK(bSucceeded);
			CHECK(HttpResponse->GetResponseCode() == 200);
		});
		HttpRequest->ProcessRequest();
	}
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can early wake up when adding new request", HTTP_TAG)
{
	const double StartTime = FPlatformTime::Seconds();

	// First request will wait for a long time before receiving any update
	TSharedRef<IHttpRequest> HttpRequestSlow = FHttpModule::Get().CreateRequest();
	HttpRequestSlow ->SetURL(UrlStreamDownload(1, HTTP_TEST_TIMEOUT_CHUNK_SIZE, /*ChunkLatency=*/3));
	HttpRequestSlow->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);

	HttpRequestSlow->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequestSlow, FHttpResponsePtr HttpResponse, bool bSucceeded)
	{
		CHECK(!bSucceeded);
	});
	HttpRequestSlow->ProcessRequest();

	FPlatformProcess::Sleep(1);

	// Second request will complete immediately
	TSharedRef<IHttpRequest> HttpRequestFast = FHttpModule::Get().CreateRequest();
	HttpRequestFast->SetURL(UrlStreamDownload(3, HTTP_TEST_TIMEOUT_CHUNK_SIZE, /*ChunkLatency=*/ 0));
	HttpRequestFast->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
	HttpRequestFast->OnProcessRequestComplete().BindLambda([StartTime, HttpRequestSlow](FHttpRequestPtr HttpRequestFast, FHttpResponsePtr HttpResponse, bool bSucceeded)
	{
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);

		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		CHECK(DurationInSeconds < 2.0f);

		CHECK(HttpRequestSlow->GetStatus() == EHttpRequestStatus::Processing);
		HttpRequestSlow->CancelRequest();
	});
	HttpRequestFast->ProcessRequest();
}

#if HAS_EVENTLOOP_NOTIFIER_EVENTFD
class FPerfAsyncEventLoopNotifierHttpFixture : public FHttpModuleTestFixture
{
public:
	FPerfAsyncEventLoopNotifierHttpFixture()
	{
		// For FEventLoopNotifierEventFd::Notify to use thread pool in WebTests
		CHECK(GIOThreadPool == nullptr);
		GIOThreadPool = FQueuedThreadPool::Allocate();
		int32 NumThreadsInThreadPool = FPlatformMisc::NumberOfIOWorkerThreadsToSpawn();
		REQUIRE(GIOThreadPool->Create(NumThreadsInThreadPool, 96 * 1024, TPri_AboveNormal, TEXT("IOThreadPool")));
	}

	~FPerfAsyncEventLoopNotifierHttpFixture()
	{
		GIOThreadPool->Destroy();
		GIOThreadPool = nullptr;
	}
};

TEST_CASE_METHOD(FPerfAsyncEventLoopNotifierHttpFixture, "Can use event loop notify with good performance", HTTP_TAG)
#else // HAS_EVENTLOOP_NOTIFIER_EVENTFD
TEST_CASE_METHOD(FHttpModuleTestFixture, "Can use event loop notify with good performance", HTTP_TAG)
#endif // HAS_EVENTLOOP_NOTIFIER_EVENTFD
{
	DisableWarningsInThisTest(); // for: [FHttpManager::Shutdown] Unbinding delegates for 5 outstanding Http Requests

	for (int32 i = 0; i < 20; ++i)
	{
		TSharedRef<IHttpRequest> HttpRequest = FHttpModule::Get().CreateRequest();
		HttpRequest->SetURL(UrlStreamDownload(3, HTTP_TEST_TIMEOUT_CHUNK_SIZE, /*ChunkLatency=*/ 0));
		HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);

		HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
		{
			CHECK(bSucceeded);
			REQUIRE(HttpResponse != nullptr);
			CHECK(HttpResponse->GetResponseCode() == 200);
		});

		const double StartTime = FPlatformTime::Seconds();
		HttpRequest->ProcessRequest();
		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		REQUIRE(DurationInSeconds < 0.001); // It shouldn't exceed 1ms on any platform

		FPlatformProcess::Sleep(0.005f);
	}
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can run a lot of small download requests with good performance", HTTP_TAG)
{
	if (!bRunHeavyTests)
	{
		return;
	}

	const double StartTime = FPlatformTime::Seconds();

	int32 NumRequests = 10000;

	std::atomic<int32> RunningRequests = 0;
	for (int32 i = 0; i < NumRequests; ++i)
	{
		TSharedRef<IHttpRequest> HttpRequest = FHttpModule::Get().CreateRequest();
		HttpRequest->SetURL(UrlStreamDownload(3, HTTP_TEST_TIMEOUT_CHUNK_SIZE, /*ChunkLatency=*/ 0));
		HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);

		HttpRequest->OnProcessRequestComplete().BindLambda([&RunningRequests](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
		{
			CHECK(bSucceeded);
			REQUIRE(HttpResponse != nullptr);
			CHECK(HttpResponse->GetResponseCode() == 200);
			--RunningRequests;
		});

		// Add requests constantly to validate early wake up performance instead of adding all at beginning
		while (RunningRequests > 20)
		{
			FPlatformProcess::Sleep(0.0f);
		}

		++RunningRequests;
		HttpRequest->ProcessRequest();
	}

	WaitUntilAllHttpRequestsComplete();

	const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;

	UE_LOGF(LogHttp, Log, "%d small download requests spent %.2fs to complete!", NumRequests, DurationInSeconds);
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can download big file exceeds 32 bits with good performance", HTTP_TAG)
{
	if (!bRunHeavyTests)
	{
		return;
	}

	const double StartTime = FPlatformTime::Seconds();

	// 5 * 1024 * 1024 * 1024 BYTES = 5368709120 BYTES = 5 GB
	uint64 DataSizeInGB = 5;
	uint64 Chunks = DataSizeInGB * 1024;
	uint64 ChunkSize = 1024 * 1024;

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamDownload(Chunks, ChunkSize));
	HttpRequest->SetVerb(TEXT("GET"));

	TSharedRef<int64> TotalBytesReceived = MakeShared<int64>(0);
	FHttpRequestStreamDelegateV2 Delegate;
	Delegate.BindLambda([TotalBytesReceived](void* Ptr, int64& Length) {
		*TotalBytesReceived += Length;
	});
	HttpRequest->SetResponseBodyReceiveStreamDelegateV2(Delegate);

	HttpRequest->OnProcessRequestComplete().BindLambda([Chunks, ChunkSize, TotalBytesReceived](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);
		CHECK(HttpResponse->GetContent().IsEmpty());
		CHECK(*TotalBytesReceived == Chunks * ChunkSize);
		CHECK(HttpResponse->GetResponseCode() == 200);
	});
	HttpRequest->ProcessRequest();

	WaitUntilAllHttpRequestsComplete();

	const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;

	UE_LOGF(LogHttp, Log, "%d GB of big download request spent %.2fs to complete!", DataSizeInGB, DurationInSeconds);
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can download big file exceeds 32 bits without stream delegate", HTTP_TAG)
{
	DisableWarningsInThisTest();

	if (!bRunHeavyTests) // TODO: This can be removed once we can make the http request fail early. 
	{
		return;
	}

	const double StartTime = FPlatformTime::Seconds();

	// Smaller size to stay within int32 limits for in-memory array: 2GB, which is close to int32::Max (MAX_int32 = 2147483647)
	constexpr uint64 DataSizeInGB = 2;
	constexpr uint64 Chunks = DataSizeInGB * 1024;
	constexpr uint64 ChunkSize = 1024 * 1024;

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamDownload(Chunks, ChunkSize));
	HttpRequest->SetVerb(TEXT("GET"));

	HttpRequest->OnProcessRequestComplete().BindLambda([Chunks, ChunkSize](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
	{
		CHECK(!bSucceeded);
		CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::ResponseTooLarge);
		// REQUIRE(HttpResponse == nullptr); // TODO: HttpResponse needs to be NULL when failed due to ResponseTooLarge
	});
	HttpRequest->ProcessRequest();

	WaitUntilAllHttpRequestsComplete();

	const double DurationInSeconds = FPlatformTime::Seconds() - StartTime;

	UE_LOGF(LogHttp, Log, "%llu GB download without stream delegate spent %.2fs to complete!", DataSizeInGB, DurationInSeconds);
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Streaming http upload from memory", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(FString::Format(TEXT("{0}/streaming_upload_post"), { *UrlHttpTests() }));
	HttpRequest->SetVerb(TEXT("POST"));

	const char* BoundaryLabel = "test_http_boundary";
	HttpRequest->SetHeader(TEXT("Content-Type"), FString::Format(TEXT("multipart/form-data; boundary={0}"), { BoundaryLabel }));

	// Data will be sent by chunks in http request
	const uint32 FileSize = 10*1024*1024;
	char* FileData = (char*)FMemory::Malloc(FileSize + 1);
	FMemory::Memset(FileData, 'd', FileSize);
	FileData[FileSize + 1] = '\0';

	TArray<uint8> ContentData;
	const int32 ContentMaxSize = FileSize + 256/*max length of format string*/;
	ContentData.Reserve(ContentMaxSize);
	const int32 ContentLength = FCStringAnsi::Snprintf(
		(char*)ContentData.GetData(),
		ContentMaxSize,
		"--%s\r\n"
		"Content-Disposition: form-data; name=\"file\"; filename=\"bigfile.zip\"\r\n"
		"Content-Type: application/octet-stream\r\n\r\n"
		"%s\r\n"
		"--%s--",
		BoundaryLabel, FileData, BoundaryLabel);

	FMemory::Free(FileData);

	CHECK(ContentLength > 0);
	CHECK(ContentLength < ContentMaxSize);
	ContentData.SetNumUninitialized(ContentLength);
	HttpRequest->SetContent(MoveTemp(ContentData));

	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);
	});
	HttpRequest->ProcessRequest();
}

class FTestHttpUploadStream final : public FArchive
{
public:
	FTestHttpUploadStream(uint64 InTotalSize)
		: FakeTotalSize(InTotalSize)
	{
	}

	virtual void Serialize(void* V, int64 Length) override
	{
		for (int64 i = 0; i < Length; ++i)
		{
			((char*)V)[i] = 'd';
		}

		CurrentPos += Length;
	}

	virtual int64 TotalSize() override
	{
		return FakeTotalSize;
	}

	virtual void Seek(int64 InPos) 
	{ 
		CurrentPos = InPos;
	}

	virtual int64 Tell() override
	{
		return CurrentPos;
	}

	uint64 FakeTotalSize;
	uint64 CurrentPos = 0;
};

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can upload big file exceeds 32 bits", HTTP_TAG)
{
	if (!bRunHeavyTests)
	{
		return;
	}

	// TODO: Back to check later. xCurl 2206.4.0.0 doesn't work with file bigger than 32 bits
	// 5 * 1024 * 1024 * 1024 BYTES = 5368709120 BYTES = 5 GB
	//const uint64 TotalSize = 5368709120;
	//const uint64 TotalSize = 4294967296;
	//const uint64 TotalSize = 4294967295;
	//const uint64 TotalSize = 2147483648;
	const uint64 TotalSize = 2147483647;
	TSharedRef<FTestHttpUploadStream> Stream = MakeShared<FTestHttpUploadStream>(TotalSize);

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamUpload());
	HttpRequest->SetVerb(TEXT("PUT"));
	HttpRequest->SetContentFromStream(Stream);
	HttpRequest->SetHeader(TEXT("Content-Disposition"), TEXT("attachment;filename=TestStreamUpload.dat"));
	HttpRequest->OnProcessRequestComplete().BindLambda([Stream, TotalSize](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);
		CHECK(Stream->CurrentPos == TotalSize);
	});
	HttpRequest->ProcessRequest();
}

namespace UE
{
namespace TestHttp
{

void WriteTestFile(const FString& TestFileName, uint64 TestFileSize)
{
	FArchive* RawFile = IFileManager::Get().CreateFileWriter(*TestFileName);
	CHECK(RawFile != nullptr);
	TSharedRef<FArchive> FileToWrite = MakeShareable(RawFile);
	char* FileData = (char*)FMemory::Malloc(TestFileSize);
	FMemory::Memset(FileData, 'd', TestFileSize);
	FileToWrite->Serialize(FileData, TestFileSize);
	FileToWrite->FlushCache();
	FileToWrite->Close();
	FMemory::Free(FileData);
}

}
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http request activity timeout", HTTP_TAG)
{
	DisableWarningsInThisTest();

	float ActivityTimeoutSetting = 3.0f;
	HttpModule->HttpActivityTimeout = ActivityTimeoutSetting;

	TSharedPtr<IHttpRequest> HttpRequest = CreateRequest();

	SECTION("By default activity timeout from http module")
	{
	}
	SECTION("By customized activity timeout per http request which will override default settings from http module")
	{
		ActivityTimeoutSetting = 4.0f;
		HttpRequest->SetActivityTimeout(ActivityTimeoutSetting);
	}

	HttpRequest->SetURL(UrlStreamDownload(3/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 5/*ChunkLatency*/));
	HttpRequest->SetVerb(TEXT("GET"));

	const double StartTime = FPlatformTime::Seconds();

	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime, ActivityTimeoutSetting](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(!bSucceeded);
		CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::ConnectionError);

		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		// Headers deferred until first chunk arrives, activity timer starts then. Timeout fires at roughly header_arrival + ActivityTimeoutSetting
		// On different platforms, header deferral timing can vary significantly, so just verify it didn't timeout too early
		CHECK(DurationInSeconds >= ActivityTimeoutSetting);

	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http request won't trigger activity timeout after cancelling", HTTP_TAG)
{
	HttpModule->HttpActivityTimeout = 2.0f;

	TSharedPtr<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamDownload(3/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 5/*ChunkLatency*/));
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);

	const double TimeToWaitBeforeCancel = 1.0f;
	const double StartTime = FPlatformTime::Seconds();
	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime, TimeToWaitBeforeCancel](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, TimeToWaitBeforeCancel, HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST));
		CHECK(!bSucceeded);
		CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::Cancelled);
	});
	HttpRequest->ProcessRequest();
	FPlatformProcess::Sleep(TimeToWaitBeforeCancel);
	HttpRequest->CancelRequest();
	FPlatformProcess::Sleep(3.0f); // Just make sure there is no warning or assert triggered by the activity timeout callback
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http request won't trigger activity timeout after total timeout", HTTP_TAG)
{
	DisableWarningsInThisTest();

	HttpModule->HttpActivityTimeout = 2.0f;
	HttpModule->HttpTotalTimeout = 3.5f;

	TSharedPtr<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamDownload(5/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 1/*ChunkLatency*/));
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);

	const double StartTime = FPlatformTime::Seconds();
	HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(!bSucceeded);
		CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::TimedOut);
		ResumeLogVerbosity();
	});
	HttpRequest->ProcessRequest();
	FPlatformProcess::Sleep(6.0f); // Just make sure there is no warning or assert triggered by the activity timeout callback
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http request receive won't timeout for streaming request", HTTP_TAG)
{
	HttpModule->HttpActivityTimeout = 3.0f;

	TSharedPtr<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamDownload(3/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 2/*ChunkLatency*/)); // Needs 6s to complete
	HttpRequest->SetVerb(TEXT("GET"));

	const double StartTime = FPlatformTime::Seconds();
	HttpRequest->OnProcessRequestComplete().BindLambda([this, StartTime](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);
		const double DurationInSeconds = FPlatformTime::Seconds() - StartTime;
		CHECK(DurationInSeconds > HttpModule->HttpActivityTimeout);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http request total timeout with get", HTTP_TAG)
{
	DisableWarningsInThisTest();

	float TotalTimeoutSetting = 3.0f;
	HttpModule->HttpConnectionTimeout = 5.0f;

	TSharedPtr<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlMockLatency(10));
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetTimeout(TotalTimeoutSetting);

	const double StartTime = FPlatformTime::Seconds();

	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime, TotalTimeoutSetting](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(!bSucceeded);
		CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::TimedOut);
		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, TotalTimeoutSetting, HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST));
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http request total timeout with streaming download", HTTP_TAG)
{
	DisableWarningsInThisTest();

	float TimeoutSetting = 3.0f;
	HttpModule->HttpActivityTimeout = 2.5f; // Make sure it won't fail because of receive timeout
	HttpModule->HttpTotalTimeout = TimeoutSetting;

	if (bRetryEnabled)
	{
		TimeoutSetting = 4.0f; // This will override http module default timeout
		HttpRetryManager->RetryTimeoutRelativeSecondsDefault = TimeoutSetting;
	}

	TSharedPtr<IHttpRequest> HttpRequest;
	SECTION("Use default timeout from http module or retry manager depends on bRetryEnabled")
	{
		HttpRequest = CreateRequest();
	}
	SECTION("Override from http request")
	{
		TimeoutSetting = 5.0f; // This will override default timeout in http module and retry manager

		if (bRetryEnabled)
		{
			HttpRequest = HttpRetryManager->CreateRequest(FHttpRetrySystem::FRetryLimitCountSetting(), TimeoutSetting);
		}
		else
		{
			HttpRequest = HttpModule->CreateRequest();
			HttpRequest->SetTimeout(TimeoutSetting);
		}
	}

	HttpRequest->SetURL(UrlStreamDownload(4/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 2/*ChunkLatency*/)); // Needs 8s to complete
	HttpRequest->SetVerb(TEXT("GET"));

	const double StartTime = FPlatformTime::Seconds();

	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime, TimeoutSetting](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(!bSucceeded);
		CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::TimedOut);
		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, TimeoutSetting, HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST));
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Streaming http upload from file by PUT can work well", HTTP_TAG)
{
	FString Filename = FString(FPlatformProcess::UserSettingsDir()) / TEXT("TestStreamUpload.dat");
	UE::TestHttp::WriteTestFile(Filename, 5*1024*1024/*5MB*/);

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamUpload());
	HttpRequest->SetVerb(TEXT("PUT"));
	HttpRequest->SetHeader(TEXT("Content-Disposition"), TEXT("attachment;filename=TestStreamUpload.dat"));
	HttpRequest->SetContentAsStreamedFile(Filename);
	HttpRequest->OnProcessRequestComplete().BindLambda([Filename](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		CHECK(HttpResponse->GetResponseCode() == 200);
		IFileManager::Get().Delete(*Filename);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Streaming http upload from invalid file by PUT will fail", HTTP_TAG)
{
	DisableWarningsInThisTest();
	FString Filename = FString(FPlatformProcess::UserSettingsDir()) / TEXT("TestStreamUploadNonExist.dat");

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamUpload());
	HttpRequest->SetVerb(TEXT("PUT"));
	HttpRequest->SetHeader(TEXT("Content-Disposition"), TEXT("attachment;filename=TestStreamUploadNonExist.dat"));
	HttpRequest->SetContentAsStreamedFile(Filename);
	HttpRequest->OnProcessRequestComplete().BindLambda([Filename](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(!bSucceeded);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::Other);
		CHECK(!HttpResponse);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Streaming uploading http request will re-open file when retry", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}
	DisableWarningsInThisTest();

	FString Filename = FString(FPlatformProcess::UserSettingsDir()) / TEXT("TestStreamUploadRetry.dat");
	UE::TestHttp::WriteTestFile(Filename, 1*1024*1024/*1MB*/);

	TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
		1/*InRetryLimitCountOverride*/,
		FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting()/*InRetryTimeoutRelativeSecondsOverride unused*/,
		{EHttpResponseCodes::TooManyRequests}/*InRetryResponseCodes*/
	);

	HttpRequest->SetURL(UrlMockStatus(EHttpResponseCodes::TooManyRequests));
	HttpRequest->SetHeader(TEXT("Retry-After"), TEXT("1")); // Will be forwarded back in response
	HttpRequest->SetVerb(TEXT("PUT"));
	HttpRequest->SetHeader(TEXT("Content-Disposition"), TEXT("attachment;filename=TestStreamUploadRetry.dat"));
	HttpRequest->SetContentAsStreamedFile(Filename);

	++ExpectingExtraCallbacks;
	HttpRequest->OnRequestWillRetry().BindLambda([this](FHttpRequestPtr Request, FHttpResponsePtr /*Response*/, float LockoutPeriod) {
		--ExpectingExtraCallbacks;
		Request->SetURL(UrlStreamUpload());
	});

	HttpRequest->OnProcessRequestComplete().BindLambda([Filename](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		CHECK(HttpResponse->GetResponseCode() == 200);
		IFileManager::Get().Delete(*Filename);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Redirect enabled by default and can work well", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	FString OriginalURL = FString::Format(TEXT("{0}/redirect_from"), { *UrlHttpTests() });
	FString ExpectedURL = FString::Format(TEXT("{0}/redirect_to"), { *UrlHttpTests() });
	HttpRequest->SetURL(OriginalURL);
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->OnProcessRequestComplete().BindLambda([OriginalURL, ExpectedURL](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		CHECK(HttpResponse->GetResponseCode() == 200);
		CHECK(HttpResponse->GetURL() == OriginalURL);
		CHECK(HttpResponse->GetEffectiveURL() == ExpectedURL);
	});
	HttpRequest->ProcessRequest();
}

class FWaitUntilQuitFromTestFixture : public FWaitUntilCompleteHttpFixture
{
public:
	FWaitUntilQuitFromTestFixture()
	{
	}

	~FWaitUntilQuitFromTestFixture()
	{
		WaitUntilQuitFromTest();
	}

	void WaitUntilQuitFromTest()
	{
		while (!bQuitRequested)
		{
			TickHttpManager();
		}
	}

	bool bQuitRequested = false;
};

TEST_CASE_METHOD(FWaitUntilQuitFromTestFixture, "Http request can be reused", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlToTestMethods());
	HttpRequest->SetVerb(TEXT("POST"));

	HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		CHECK(HttpResponse->GetResponseCode() == 200);

		// Using a different URL
		uint32 Chunks = 3;
		uint32 ChunkSize = 1024;
		HttpRequest->SetURL(UrlStreamDownload(Chunks, ChunkSize));
		HttpRequest->SetVerb(TEXT("GET"));
		HttpRequest->OnProcessRequestComplete().BindLambda([this, Chunks, ChunkSize](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
			REQUIRE(HttpResponse != nullptr);
			CHECK(HttpResponse->GetResponseCode() == 200);
			CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);

			// Simulate retry with same URL info
			HttpRequest->OnProcessRequestComplete().BindLambda([this, Chunks, ChunkSize](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
				CHECK(bSucceeded);
				REQUIRE(HttpResponse != nullptr);
				CHECK(HttpResponse->GetResponseCode() == 200);
				CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);
				bQuitRequested = true;
			});
			HttpRequest->ProcessRequest();
		});
		HttpRequest->ProcessRequest();
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilQuitFromTestFixture, "Http request can be reused when there is total timeout setting", HTTP_TAG)
{
	DisableWarningsInThisTest();

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlMockLatency(3));
	HttpRequest->SetTimeout(2);

	HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(!bSucceeded);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::TimedOut);

		HttpRequest->SetURL(UrlMockLatency(1));
		HttpRequest->ResetTimeoutStatus(); // Must do this in order to restart timeout

		HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
			bQuitRequested = true;
		});
		HttpRequest->ProcessRequest();
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilQuitFromTestFixture, "Make sure connection time out can work well for 2nd same http request", HTTP_TAG)
{
	DisableWarningsInThisTest();

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();

	float ConnectionTimeoutDuration = 2.0f;
	HttpModule->HttpConnectionTimeout = ConnectionTimeoutDuration;

	SetInvalidDomainToTestConnectTimeout();
	HttpRequest->SetURL(UrlToTestMethods());

	const double StartTime = FPlatformTime::Seconds();

	HttpRequest->OnProcessRequestComplete().BindLambda([this, StartTime, ConnectionTimeoutDuration](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		TSharedRef<IHttpRequest> HttpRequest2 = CreateRequest();
		HttpRequest2->SetURL(UrlToTestMethods());
		HttpRequest2->OnProcessRequestComplete().BindLambda([this, StartTime, ConnectionTimeoutDuration](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			bQuitRequested = true;
			const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
			CHECK(FMath::IsNearlyEqual(DurationInSeconds, ConnectionTimeoutDuration * 2, UE_HTTP_CONNECTION_TIMEOUT_MAX_DEVIATION * 2));
		});
		HttpRequest2->ProcessRequest();
	});
	HttpRequest->ProcessRequest();
}

// Response shared ptr should be able to be kept by user code and valid to access without http request
class FValidateResponseDependencyFixture : public FWaitUntilCompleteHttpFixture
{
public:
	DECLARE_DELEGATE(FValidateResponseDependencyDelegate);

	~FValidateResponseDependencyFixture()
	{
		WaitUntilAllHttpRequestsComplete();

		ValidateResponseDependencyDelegate.ExecuteIfBound();
	}

	FValidateResponseDependencyDelegate ValidateResponseDependencyDelegate;
};

TEST_CASE_METHOD(FValidateResponseDependencyFixture, "Http query with parameters", HTTP_TAG)
{
	struct FQueryWithParamsResponse : public FJsonSerializable
	{
		int32 VarInt;
		FString VarStr;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE("var_int", VarInt);
			JSON_SERIALIZE("var_str", VarStr);
		END_JSON_SERIALIZER
	};

	TSharedRef<IHttpRequest> HttpRequest = HttpModule->CreateRequest();
	FString UrlQueryWithParams = FString::Format(TEXT("{0}/query_with_params/?var_int=3&var_str=abc"), { *UrlHttpTests() });
	HttpRequest->SetURL(UrlQueryWithParams);
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->OnProcessRequestComplete().BindLambda([this, UrlQueryWithParams](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);

		CHECK(HttpRequest->GetURL() == UrlQueryWithParams);

		// Validate GetContentAsUtf8StringView works well
		FQueryWithParamsResponse QueryWithParamsResponseThroughStringView;
		REQUIRE(QueryWithParamsResponseThroughStringView.FromJsonStringView(HttpResponse->GetContentAsUtf8StringView()));

		CHECK(FString::FromInt(QueryWithParamsResponseThroughStringView.VarInt) == HttpRequest->GetURLParameter(TEXT("var_int")));
		CHECK(QueryWithParamsResponseThroughStringView.VarStr == HttpRequest->GetURLParameter(TEXT("var_str")));

		CHECK(FString::FromInt(QueryWithParamsResponseThroughStringView.VarInt) == HttpResponse->GetURLParameter(TEXT("var_int")));
		CHECK(QueryWithParamsResponseThroughStringView.VarStr == HttpResponse->GetURLParameter(TEXT("var_str")));

		// Validate GetContentAsString works well
		FQueryWithParamsResponse QueryWithParamsResponse;
		REQUIRE(QueryWithParamsResponse.FromJson(HttpResponse->GetContentAsString()));

		CHECK(FString::FromInt(QueryWithParamsResponse.VarInt) == HttpRequest->GetURLParameter(TEXT("var_int")));
		CHECK(QueryWithParamsResponse.VarStr == HttpRequest->GetURLParameter(TEXT("var_str")));

		CHECK(FString::FromInt(QueryWithParamsResponse.VarInt) == HttpResponse->GetURLParameter(TEXT("var_int")));
		CHECK(QueryWithParamsResponse.VarStr == HttpResponse->GetURLParameter(TEXT("var_str")));

		ValidateResponseDependencyDelegate.BindLambda([HttpResponse, UrlQueryWithParams, QueryWithParamsResponse](){
			// Validate all interfaces of http response can be called without accessing the destroyed http request
			CHECK(HttpResponse->GetResponseCode() == 200);
			CHECK(!HttpResponse->GetContent().IsEmpty());
			CHECK(!HttpResponse->GetContentAsString().IsEmpty());
			CHECK(HttpResponse->GetContentType() == TEXT("application/json"));
			CHECK(HttpResponse->GetHeader("Content-Type") == TEXT("application/json"));
			CHECK(!HttpResponse->GetAllHeaders().IsEmpty());
			CHECK(HttpResponse->GetURL() == UrlQueryWithParams);
			CHECK(HttpResponse->GetURLParameter(TEXT("var_int")) == FString::FromInt(QueryWithParamsResponse.VarInt));
			CHECK(HttpResponse->GetURLParameter(TEXT("var_str")) == QueryWithParamsResponse.VarStr);
		});
	});
	HttpRequest->ProcessRequest();
}

class FThreadedHttpRunnable : public FRunnable
{
public:
	DECLARE_DELEGATE(FRunActualTestCodeDelegate);

	FRunActualTestCodeDelegate& OnRunFromThread()
	{
		return ThreadCallback;
	}

	// FRunnable interface
	virtual uint32 Run() override
	{
		ThreadCallback.ExecuteIfBound();
		return 0;
	}

	void StartTestHttpThread(bool bBlockGameThread)
	{
		bBlockingGameThreadTick = bBlockGameThread;

		RunnableThread = TSharedPtr<FRunnableThread>(FRunnableThread::Create(this, TEXT("Test Http Thread")));

		while (bBlockingGameThreadTick)
		{
			float TickFrequency = 1.0f / 60; /*60 FPS*/;
			FPlatformProcess::Sleep(TickFrequency);
		}
	}

	void UnblockGameThread()
	{
		bBlockingGameThreadTick = false;
	}

private:
	FRunActualTestCodeDelegate ThreadCallback;
	TSharedPtr<FRunnableThread> RunnableThread;
	std::atomic<bool> bBlockingGameThreadTick = true;
};

class FWaitThreadedHttpFixture : public FWaitUntilCompleteHttpFixture
{
public:
	~FWaitThreadedHttpFixture()
	{
		WaitUntilAllHttpRequestsComplete();
	}

	FThreadedHttpRunnable ThreadedHttpRunnable;
};

TEST_CASE_METHOD(FWaitThreadedHttpFixture, "Http streaming download request can work in non game thread", HTTP_TAG)
{
	ThreadedHttpRunnable.OnRunFromThread().BindLambda([this]() {
		TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
		HttpRequest->SetURL(UrlStreamDownload(3/*Chunks*/, 1024/*ChunkSize*/));
		HttpRequest->SetVerb(TEXT("GET"));
		HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);

		class FTestHttpReceiveStream final : public FArchive
		{
		public:
			virtual void Serialize(void* V, int64 Length) override
			{
				// No matter what's the thread policy, Serialize always get called in http thread.
				CHECK(!IsInGameThread());
			}
		};
		CHECK(HttpRequest->SetResponseBodyReceiveStream(MakeShared<FTestHttpReceiveStream>()));

		HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			// EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread was used, so not in game thread here
			CHECK(!IsInGameThread());
			CHECK(bSucceeded);
			REQUIRE(HttpResponse != nullptr);
			CHECK(HttpResponse->GetResponseCode() == 200);
			CHECK(!HttpResponse->GetAllHeaders().IsEmpty());
			ThreadedHttpRunnable.UnblockGameThread();
		});

		HttpRequest->ProcessRequest();
	});

	ThreadedHttpRunnable.StartTestHttpThread(true/*bBlockGameThread*/);
}

TEST_CASE_METHOD(FWaitThreadedHttpFixture, "Http download request progress callback can be received in http thread", HTTP_TAG)
{
	std::atomic<bool> bRequestProgressTriggered = false;
	ThreadedHttpRunnable.OnRunFromThread().BindLambda([this, &bRequestProgressTriggered]() {
		TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
		HttpRequest->SetURL(UrlStreamDownload(10/*Chunks*/, 1024*1024/*ChunkSize*/));
		HttpRequest->SetVerb(TEXT("GET"));

		HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		HttpRequest->OnRequestProgress64().BindLambda([this, &bRequestProgressTriggered](FHttpRequestPtr Request, uint64 /*BytesSent*/, uint64 BytesReceived) {
			if (!bRequestProgressTriggered)
			{
				// Only do these checks once, because when http request complete, this callback also get triggered
				CHECK(BytesReceived > 0);
				CHECK(BytesReceived < 10/*Chunks*/ * 1024*1024/*ChunkSize*/);
				CHECK(!IsInGameThread());
				CHECK(Request->GetStatus() == EHttpRequestStatus::Processing);
				bRequestProgressTriggered = true;
			}
		});
		HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr /*HttpRequest*/, FHttpResponsePtr /*HttpResponse */, bool bSucceeded) {
			CHECK(bSucceeded);
			ThreadedHttpRunnable.UnblockGameThread();
		});

		HttpRequest->ProcessRequest();
	});

	ThreadedHttpRunnable.StartTestHttpThread(true/*bBlockGameThread*/);

	CHECK(bRequestProgressTriggered);
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http request pre check will fail", HTTP_TAG)
{
	DisableWarningsInThisTest();

	TSharedRef<IHttpRequest> HttpRequest = HttpModule->CreateRequest();

	SECTION("when verb was set to empty")
	{
		HttpRequest->SetURL(UrlToTestMethods());
		HttpRequest->SetVerb(TEXT(""));
	}
	SECTION("when url protocol is not valid")
	{
		HttpRequest->SetURL("http_abc://www.epicgames.com");
		HttpRequest->SetVerb(TEXT("GET"));
	}
	SECTION("when url was not set")
	{
		HttpRequest->SetVerb(TEXT("GET"));
	}

	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(!bSucceeded);
	});

	HttpRequest->ProcessRequest();
}

class FValidateHeaderReceiveOrderFixture : public FWaitUntilCompleteHttpFixture
{
public:
	~FValidateHeaderReceiveOrderFixture()
	{
		WaitUntilAllHttpRequestsComplete();
	}

	std::atomic<bool> bHeaderReceived = false;
	std::atomic<bool> bCompleteCallbackTriggered = false;
	std::atomic<bool> bAnyDataReceived = false;
};

TEST_CASE_METHOD(FValidateHeaderReceiveOrderFixture, "Http request header received callback will be called by thread policy", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamDownload(2/*Chunks*/, 1024/*ChunkSize*/));
	HttpRequest->SetVerb(TEXT("GET"));

	FHttpRequestStreamDelegateV2 StreamDelegate;
	StreamDelegate.BindLambda([this](void *InDataPtr, int64& InLength) {
		bAnyDataReceived = true;
		CHECK(!bCompleteCallbackTriggered);
	});
	HttpRequest->SetResponseBodyReceiveStreamDelegateV2(StreamDelegate);

	SECTION("in http thread")
	{
		HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		HttpRequest->OnHeaderReceived().BindLambda([this](FHttpRequestPtr Request, const FString& HeaderName, const FString& HeaderValue) {
			CHECK(!bAnyDataReceived);
			CHECK(!bCompleteCallbackTriggered);
			CHECK(!IsInGameThread());
			bHeaderReceived = true;
		});
	}
	SECTION("in game thread")
	{
		HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
		HttpRequest->OnHeaderReceived().BindLambda([this](FHttpRequestPtr Request, const FString& HeaderName, const FString& HeaderValue) {
			// Data received delegate always triggered from http thread, so it could have been received, while header will be received
			// from game thread in this test section
			// CHECK(!bAnyDataReceived);
			CHECK(!bCompleteCallbackTriggered);
			CHECK(IsInGameThread());
			bHeaderReceived = true;
		});
	}

	HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr /*HttpRequest*/, FHttpResponsePtr /*HttpResponse */, bool bSucceeded) {
		CHECK(bAnyDataReceived);
		CHECK(bHeaderReceived);
		bCompleteCallbackTriggered = true;
		CHECK(bSucceeded);
	});

	HttpRequest->ProcessRequest();
}

class FValidateStatusCodeReceiveOrderFixture : public FWaitUntilCompleteHttpFixture
{
public:
	~FValidateStatusCodeReceiveOrderFixture()
	{
		WaitUntilAllHttpRequestsComplete();
	}

	std::atomic<bool> bStatusCodeReceived = false;
	std::atomic<bool> bCompleteCallbackTriggered = false;
};

TEST_CASE_METHOD(FValidateStatusCodeReceiveOrderFixture, "Http request status code received callback will be called by thread policy", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamDownload(20/*Chunks*/, 1024*1024/*ChunkSize*/));
	HttpRequest->SetVerb(TEXT("GET"));

	SECTION("in http thread")
	{
		HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		HttpRequest->OnStatusCodeReceived().BindLambda([this](FHttpRequestPtr Request, int32 StatusCode) {
			CHECK(StatusCode == 200);
			CHECK(!bCompleteCallbackTriggered);
			CHECK(!IsInGameThread());
			bStatusCodeReceived = true;
		});
	}
	SECTION("in game thread")
	{
		HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
		HttpRequest->OnStatusCodeReceived().BindLambda([this](FHttpRequestPtr Request, int32 StatusCode) {
			CHECK(StatusCode == 200);
			CHECK(!bCompleteCallbackTriggered);
			CHECK(IsInGameThread());
			bStatusCodeReceived = true;
		});
	}

	HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr /*HttpRequest*/, FHttpResponsePtr /*HttpResponse */, bool bSucceeded) {
		CHECK(bStatusCodeReceived);
		bCompleteCallbackTriggered = true;
		CHECK(bSucceeded);
	});

	HttpRequest->ProcessRequest();
}

namespace UE
{
namespace TestHttp
{

void SetupURLRequestFilter(FHttpModule* HttpModule)
{
	// Pre check will fail when domain is not allowed
	UE::Core::FURLRequestFilter::FRequestMap SchemeMap;
	SchemeMap.Add(TEXT("http"), TArray<FString>{TEXT("epicgames.com")});
	UE::Core::FURLRequestFilter Filter{SchemeMap};
	HttpModule->GetHttpManager().SetURLRequestFilter(Filter);
}

}
}

// Pre-check failed requests won't be added into http manager, so it can't rely on the requested added/completed callback in FWaitUntilCompleteHttpFixture
TEST_CASE_METHOD(FWaitUntilQuitFromTestFixture, "Http request pre check will fail by thread policy", HTTP_TAG)
{
	DisableWarningsInThisTest();

	// Pre check will fail when domain is not allowed
	UE::TestHttp::SetupURLRequestFilter(HttpModule);

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetURL(UrlToTestMethods());

	SECTION("on game thread")
	{
		HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(IsInGameThread());
			CHECK(!bSucceeded);
			bQuitRequested = true;
		});
	}
	SECTION("on http thread")
	{
		HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(!IsInGameThread());
			CHECK(!bSucceeded);
			bQuitRequested = true;
		});
	}

	HttpRequest->ProcessRequest();
}

class FWaitUntilQuitFromTestThreadedFixture : public FWaitUntilQuitFromTestFixture
{
public:
	~FWaitUntilQuitFromTestThreadedFixture()
	{
		WaitUntilQuitFromTest();
	}

	FThreadedHttpRunnable ThreadedHttpRunnable;
};

// Pre-check failed requests won't be added into http manager, so it can't rely on the requested added/completed callback in FWaitUntilCompleteHttpFixture
TEST_CASE_METHOD(FWaitUntilQuitFromTestThreadedFixture, "Threaded http request pre check will fail by thread policy", HTTP_TAG)
{
	DisableWarningsInThisTest();

	ThreadedHttpRunnable.OnRunFromThread().BindLambda([this]() {
		// Pre check will fail when domain is not allowed
		UE::TestHttp::SetupURLRequestFilter(HttpModule);

		TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
		HttpRequest->SetVerb(TEXT("GET"));
		HttpRequest->SetURL(UrlToTestMethods());

		SECTION("on game thread")
		{
			HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
				CHECK(IsInGameThread());
				CHECK(!bSucceeded);
				bQuitRequested = true;
			});
		}
		SECTION("on http thread")
		{
			HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
			HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
				CHECK(!IsInGameThread());
				CHECK(!bSucceeded);
				bQuitRequested = true;
			});
		}

		HttpRequest->ProcessRequest();
	});

	ThreadedHttpRunnable.StartTestHttpThread(false/*bBlockGameThread*/);
}

TEST_CASE_METHOD(FWaitUntilQuitFromTestFixture, "Cancel http request without ProcessRequest called", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlToTestMethods());
	++ExpectingExtraCallbacks;
	HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		--ExpectingExtraCallbacks;
		CHECK(!bSucceeded);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::Cancelled);
		bQuitRequested = true;
	});
	HttpRequest->CancelRequest();
}

TEST_CASE_METHOD(FWaitThreadedHttpFixture, "Cancel http request with ProcessRequest called but before started from queue", HTTP_TAG)
{
	int32 OriginalMaxConcurrentRequests = CVarHttpMaxConcurrentRequests.GetValueOnAnyThread();
	CVarHttpMaxConcurrentRequests->Set(1);
	ON_SCOPE_EXIT
	{
		CVarHttpMaxConcurrentRequests->Set(OriginalMaxConcurrentRequests);
	};

	std::atomic<bool> bFirstRequestCompleted = false;

	FHttpManager& HttpManager = HttpModule->GetHttpManager();
	const FHttpStats HttpStats = HttpManager.GetHttpStats();
	CHECK(HttpStats.RequestsInQueue == 0);
	CHECK(HttpStats.MaxRequestsInQueue == 0);

	ThreadedHttpRunnable.OnRunFromThread().BindLambda([this, &bFirstRequestCompleted]() {
		TSharedRef<IHttpRequest> HttpRequestRunning = CreateRequest();
		HttpRequestRunning->SetURL(UrlStreamDownload(3/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 1/*ChunkLatency*/));
		HttpRequestRunning->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		HttpRequestRunning->OnProcessRequestComplete().BindLambda([this, &bFirstRequestCompleted](FHttpRequestPtr HttpRequestQueuing, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			bFirstRequestCompleted = true;
			ThreadedHttpRunnable.UnblockGameThread();
		});
		HttpRequestRunning->ProcessRequest();


		TSharedRef<IHttpRequest> HttpRequestQueuing = CreateRequest();
		HttpRequestQueuing->SetURL(UrlStreamDownload(3/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 1/*ChunkLatency*/));
		HttpRequestQueuing->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		HttpRequestQueuing->OnHeaderReceived().BindLambda([this](FHttpRequestPtr Request, const FString& HeaderName, const FString& HeaderValue) {
			// Should never be started
			CHECK(false);
		});
		HttpRequestQueuing->OnRequestProgress64().BindLambda([this](FHttpRequestPtr Request, uint64 /*BytesSent*/, uint64 BytesReceived) {
			// Should never be started
			CHECK(false);
		});

		++ExpectingExtraCallbacks;
		HttpRequestQueuing->OnProcessRequestComplete().BindLambda([this, &bFirstRequestCompleted](FHttpRequestPtr HttpRequestQueuing, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			--ExpectingExtraCallbacks;
			CHECK(!bSucceeded);
			CHECK(HttpRequestQueuing->GetFailureReason() == EHttpFailureReason::Cancelled);
			CHECK(!bFirstRequestCompleted);
		});
		HttpRequestQueuing->ProcessRequest();
		FPlatformProcess::Sleep(1); // Make sure the first request started

		FHttpManager& HttpManager = HttpModule->GetHttpManager();
		const FHttpStats HttpStats = HttpManager.GetHttpStats();
		CHECK(HttpStats.RequestsInQueue == 1);
		CHECK(HttpStats.MaxRequestsInQueue == 1);

		HttpRequestQueuing->CancelRequest();
	});

	ThreadedHttpRunnable.StartTestHttpThread(true/*bBlockGameThread*/);
}

TEST_CASE_METHOD(FWaitThreadedHttpFixture, "Requests with different priorities will be inserted to correct position in the queue", HTTP_TAG)
{
	int32 OriginalMaxConcurrentRequests = CVarHttpMaxConcurrentRequests.GetValueOnAnyThread();
	CVarHttpMaxConcurrentRequests->Set(1);
	ON_SCOPE_EXIT
	{
		CVarHttpMaxConcurrentRequests->Set(OriginalMaxConcurrentRequests);
	};

	std::atomic<bool> bRunningRequestCompleted = false;
	std::atomic<bool> bHttpRequestWithNormalPriorityCompleted = false;
	std::atomic<bool> bHttpRequestWithHighPriorityCompleted = false;
	std::atomic<bool> bHttpRequestWithLowPriorityCompleted = false;

	// Use thread here so we can check local bool flags in the complete delegates
	ThreadedHttpRunnable.OnRunFromThread().BindLambda([this, &bRunningRequestCompleted, &bHttpRequestWithNormalPriorityCompleted, &bHttpRequestWithHighPriorityCompleted, &bHttpRequestWithLowPriorityCompleted]() {
		TSharedRef<IHttpRequest> HttpRequestRunning = CreateRequest();
		HttpRequestRunning->SetURL(UrlStreamDownload(3/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 1/*ChunkLatency*/));
		HttpRequestRunning->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		HttpRequestRunning->OnProcessRequestComplete().BindLambda([this, &bRunningRequestCompleted, &bHttpRequestWithNormalPriorityCompleted, &bHttpRequestWithHighPriorityCompleted, &bHttpRequestWithLowPriorityCompleted](FHttpRequestPtr HttpRequestQueuing, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
			bRunningRequestCompleted = true;
			CHECK(!bHttpRequestWithNormalPriorityCompleted);
			CHECK(!bHttpRequestWithHighPriorityCompleted);
			CHECK(!bHttpRequestWithLowPriorityCompleted);
		});
		HttpRequestRunning->ProcessRequest();

		TSharedRef<IHttpRequest> HttpRequestWithNormalPriority = CreateRequest();
		HttpRequestWithNormalPriority->SetURL(UrlToTestMethods());
		HttpRequestWithNormalPriority->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		HttpRequestWithNormalPriority->OnProcessRequestComplete().BindLambda([this, &bRunningRequestCompleted, &bHttpRequestWithNormalPriorityCompleted, &bHttpRequestWithHighPriorityCompleted, &bHttpRequestWithLowPriorityCompleted](FHttpRequestPtr HttpRequestNormal, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
			CHECK(bRunningRequestCompleted);
			bHttpRequestWithNormalPriorityCompleted = true;
			CHECK(bHttpRequestWithHighPriorityCompleted);
			CHECK(!bHttpRequestWithLowPriorityCompleted);
		});
		HttpRequestWithNormalPriority->ProcessRequest();

		TSharedRef<IHttpRequest> HttpRequestWithHighPriority = CreateRequest();
		HttpRequestWithHighPriority->SetURL(UrlToTestMethods());
		HttpRequestWithHighPriority->SetPriority(EHttpRequestPriority::High);
		HttpRequestWithHighPriority->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		HttpRequestWithHighPriority->OnProcessRequestComplete().BindLambda([this, &bRunningRequestCompleted, &bHttpRequestWithNormalPriorityCompleted, &bHttpRequestWithHighPriorityCompleted, &bHttpRequestWithLowPriorityCompleted](FHttpRequestPtr HttpRequestNormal, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
			CHECK(bRunningRequestCompleted);
			CHECK(!bHttpRequestWithNormalPriorityCompleted);
			bHttpRequestWithHighPriorityCompleted = true;
			CHECK(!bHttpRequestWithLowPriorityCompleted);
		});
		HttpRequestWithHighPriority->ProcessRequest();

		TSharedRef<IHttpRequest> HttpRequestWithLowPriority = CreateRequest();
		HttpRequestWithLowPriority->SetURL(UrlToTestMethods());
		HttpRequestWithLowPriority->SetPriority(EHttpRequestPriority::Low);
		HttpRequestWithLowPriority->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		HttpRequestWithLowPriority->OnProcessRequestComplete().BindLambda([this, &bRunningRequestCompleted, &bHttpRequestWithNormalPriorityCompleted, &bHttpRequestWithHighPriorityCompleted, &bHttpRequestWithLowPriorityCompleted](FHttpRequestPtr HttpRequestNormal, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
			CHECK(bRunningRequestCompleted);
			CHECK(bHttpRequestWithNormalPriorityCompleted);
			CHECK(bHttpRequestWithHighPriorityCompleted);
			bHttpRequestWithLowPriorityCompleted = true;

			ThreadedHttpRunnable.UnblockGameThread();
		});
		HttpRequestWithLowPriority->ProcessRequest();

		FPlatformProcess::Sleep(1); // Make sure the first request started

		FHttpStats HttpStats = HttpModule->GetHttpManager().GetHttpStats();
		CHECK(HttpStats.RequestsInQueue == 3);
	});

	ThreadedHttpRunnable.StartTestHttpThread(true/*bBlockGameThread*/);
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Cancel http request connect before timeout", HTTP_TAG)
{
	DisableWarningsInThisTest();

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	SetInvalidDomainToTestConnectTimeout();
	HttpRequest->SetURL(UrlToTestMethods());
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetTimeout(7);
	const double StartTime = FPlatformTime::Seconds();
	++ExpectingExtraCallbacks;
	HttpRequest->OnProcessRequestComplete().BindLambda([this, StartTime](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		--ExpectingExtraCallbacks;
		CHECK(!bSucceeded);
		const double DurationInSeconds = FPlatformTime::Seconds() - StartTime;
		CHECK(DurationInSeconds < 4.0);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::Cancelled);
	});
	SECTION("ProcessRequest called")
	{
		HttpRequest->ProcessRequest();
		FPlatformProcess::Sleep(0.5);
	}
	SECTION("ProcessRequest not called")
	{
	}
	HttpRequest->CancelRequest();
	HttpRequest->CancelRequest(); // Duplicated calls to CancelRequest should be fine
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Retry respect Retry-After header in response", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
		1/*InRetryLimitCountOverride*/,
		FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting()/*InRetryTimeoutRelativeSecondsOverride unused*/,
		{EHttpResponseCodes::TooManyRequests, EHttpResponseCodes::ServiceUnavail}/*InRetryResponseCodes*/
	);

	SECTION("TooManyRequests")
	{
		HttpRequest->SetURL(UrlMockStatus(EHttpResponseCodes::TooManyRequests));
	}

	uint32 RetryAfter = 4;

	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetHeader(TEXT("Retry-After"), FString::Format(TEXT("{0}"), { RetryAfter })); // Will be forwarded back in response

	++ExpectingExtraCallbacks;
	HttpRequest->OnRequestWillRetry().BindLambda([this, RetryAfter](FHttpRequestPtr /*Request*/, FHttpResponsePtr /*Response*/, float LockoutPeriod) {
		--ExpectingExtraCallbacks;
		CHECK(FMath::IsNearlyEqual(LockoutPeriod, (float)(RetryAfter)));
	});

	const double StartTime = FPlatformTime::Seconds();
	HttpRequest->OnProcessRequestComplete().BindLambda([RetryAfter, StartTime](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, (float)(RetryAfter), HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST));
	});

	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Request can time out during lock out", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	EHttpRequestDelegateThreadPolicy ThreadPolicyExpected = EHttpRequestDelegateThreadPolicy::CompleteOnGameThread;
	SECTION("From game thread")
	{
	}
	SECTION("From http thread")
	{
		ThreadPolicyExpected = EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread;
	}

	TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
		1/*InRetryLimitCountOverride*/,
		FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting()/*InRetryTimeoutRelativeSecondsOverride unused*/,
		{EHttpResponseCodes::TooManyRequests}/*InRetryResponseCodes*/
	);

	HttpRequest->SetURL(UrlMockStatus(EHttpResponseCodes::TooManyRequests));
	HttpRequest->SetTimeout(1.0f);
	HttpRequest->SetDelegateThreadPolicy(ThreadPolicyExpected);

	uint32 RetryAfter = 4;

	// Will be forwarded back in response
	HttpRequest->SetHeader(TEXT("Retry-After"), FString::Format(TEXT("{0}"), { RetryAfter }));

	const double StartTime = FPlatformTime::Seconds();
	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime, ThreadPolicyExpected](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		// When timeout during lock out period, it fails with result of last request before lock out
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetFailureReason() == EHttpFailureReason::None);
		CHECK(HttpResponse->GetResponseCode() == EHttpResponseCodes::TooManyRequests);
		CHECK(HttpResponse->GetContentLength() > 0);
		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, 1.0, HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST));
		CHECK((ThreadPolicyExpected == EHttpRequestDelegateThreadPolicy::CompleteOnGameThread && IsInGameThread() || ThreadPolicyExpected == EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread && !IsInGameThread()));
	});

	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Request can time out during retry request", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
		1/*InRetryLimitCountOverride*/,
		FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting()/*InRetryTimeoutRelativeSecondsOverride unused*/,
		{EHttpResponseCodes::TooManyRequests}/*InRetryResponseCodes*/
	);

	HttpRequest->SetURL(UrlMockStatus(EHttpResponseCodes::TooManyRequests));
	HttpRequest->SetTimeout(3.0f);

	uint32 RetryAfter = 2;
	// Will be forwarded back in response
	HttpRequest->SetHeader(TEXT("Retry-After"), FString::Format(TEXT("{0}"), { RetryAfter }));

	++ExpectingExtraCallbacks;
	HttpRequest->OnRequestWillRetry().BindLambda([this](FHttpRequestPtr Request, FHttpResponsePtr /*Response*/, float LockoutPeriod) {
		--ExpectingExtraCallbacks;
		// Now retry with a latency during request
		Request->SetURL(UrlStreamDownload(3/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 2/*ChunkLatency*/));
	});

	const double StartTime = FPlatformTime::Seconds();
	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::TimedOut);

		// When timeout during retrying request, it fails with result of last request before retrying, to 
		// keep it the same behavior when timeout during lockout
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetFailureReason() == EHttpFailureReason::None);
		CHECK(HttpResponse->GetResponseCode() == EHttpResponseCodes::TooManyRequests);
		CHECK(HttpResponse->GetContentLength() > 0);
		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, 3.0, HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST));
	});

	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Request can be canceled during WillRetry delegate", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
		1/*InRetryLimitCountOverride*/,
		FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting()/*InRetryTimeoutRelativeSecondsOverride unused*/,
		{EHttpResponseCodes::TooManyRequests}/*InRetryResponseCodes*/
	);

	HttpRequest->SetURL(UrlMockStatus(EHttpResponseCodes::TooManyRequests));

	uint32 RetryAfter = 2;
	// Will be forwarded back in response
	HttpRequest->SetHeader(TEXT("Retry-After"), FString::Format(TEXT("{0}"), { RetryAfter }));

	++ExpectingExtraCallbacks;
	HttpRequest->OnRequestWillRetry().BindLambda([this](FHttpRequestPtr Request, FHttpResponsePtr /*Response*/, float LockoutPeriod) {
		--ExpectingExtraCallbacks;
		Request->CancelRequest();
	});

	const double StartTime = FPlatformTime::Seconds();
	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::Cancelled);
		CHECK(!bSucceeded);
		CHECK(HttpResponse == nullptr);

		// Will not wait for 2s(RetryAfter) if canceled during WillRetry delegate
		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, 0.0, HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST));
	});

	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Request can be canceled during back off", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}

	if (!CVarHttpCancelSupportDuringBackOffEnabled.GetValueOnAnyThread())
	{
		return;
	}

	DisableWarningsInThisTest();

	TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
		1/*InRetryLimitCountOverride*/,
		FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting()/*InRetryTimeoutRelativeSecondsOverride unused*/,
		{EHttpResponseCodes::TooManyRequests}/*InRetryResponseCodes*/
	);

	HttpRequest->SetURL(UrlMockStatus(EHttpResponseCodes::TooManyRequests));

	uint32 RetryAfter = 4;
	// Will be forwarded back in response
	HttpRequest->SetHeader(TEXT("Retry-After"), FString::Format(TEXT("{0}"), { RetryAfter }));

	++ExpectingExtraCallbacks;
	HttpRequest->OnRequestWillRetry().BindLambda([this](FHttpRequestPtr Request, FHttpResponsePtr /*Response*/, float LockoutPeriod) {
		--ExpectingExtraCallbacks;
	});

	uint32 CancelAfter = 2;

	const double StartTime = FPlatformTime::Seconds();
	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime, CancelAfter](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::Cancelled);
		CHECK(!bSucceeded);
		CHECK(HttpResponse == nullptr);

		// Will not wait for total 4s(RetryAfter) if canceled during back off
		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, CancelAfter, HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST));
	});

	HttpRequest->ProcessRequest();

	while (HasOngoingRequest())
	{
		TickHttpManager();

		if (FPlatformTime::Seconds() - StartTime > CancelAfter)
		{
			HttpRequest->CancelRequest();
			break;
		}
	}
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Request will not retry", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(1/*InRetryLimitCountOverride*/);
	SECTION("When response code is not listed for retry")
	{
		HttpRequest->SetURL(UrlMockStatus(EHttpResponseCodes::TooManyRequests));
		// Will be forwarded back in response
		HttpRequest->SetHeader(TEXT("Retry-After"), FString::Format(TEXT("{0}"), { 2 }));
	}
	SECTION("When there is any response and timed out during streaming download")
	{
		HttpRequest->SetURL(UrlStreamDownload(3/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 2/*ChunkLatency*/));
		HttpRequest->SetTimeout(3.0f);

		HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed);
			CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::TimedOut);
		});
	}

	HttpRequest->OnRequestWillRetry().BindLambda([this](FHttpRequestPtr Request, FHttpResponsePtr Response, float LockoutPeriod) {
		CHECK(false);
	});

	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Retry immediately without lock out if connect failed and there are alt domains", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	HttpModule->HttpConnectionTimeout = 1.0f;

	TArray<FString> AltDomains;
	FString CorrectDomain = UrlDomain();
	AltDomains.Add(CorrectDomain);

	FHttpRetrySystem::FRetryDomainsPtr RetryDomains = MakeShared<FHttpRetrySystem::FRetryDomains>(MoveTemp(AltDomains));
	TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
		1/*InRetryLimitCountOverride*/,
		FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting()/*InRetryTimeoutRelativeSecondsOverride unused*/,
		{ EHttpResponseCodes::TooManyRequests, EHttpResponseCodes::ServiceUnavail }/*InRetryResponseCodes*/,
		FHttpRetrySystem::FRetryVerbs(),
		RetryDomains
	);

	SetInvalidDomainToTestConnectTimeout();
	HttpRequest->SetURL(UrlToTestMethods());
	HttpRequest->SetVerb(TEXT("GET"));
	++ExpectingExtraCallbacks;
	HttpRequest->OnRequestWillRetry().BindLambda([this](FHttpRequestPtr /*Request*/, FHttpResponsePtr /*Response*/, float LockoutPeriod) {
		--ExpectingExtraCallbacks;
		CHECK(LockoutPeriod == 0);
	});
	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Retry request with CompleteOnHttpThread policy won't add same duplicated request to http manager", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	HttpModule->HttpConnectionTimeout = 1.0f;

	FString OriginalUrl;

	TArray<FString> AltDomains;
	FString CorrectDomain = UrlDomain();

	SECTION("With invalid domain")
	{
		SetInvalidDomainToTestConnectTimeout();
		OriginalUrl = UrlToTestMethods();
	}
	SECTION("With mock connection failure for valid domain")
	{
		CVarHttpUrlPatternsToMockResponse->Set(TEXT("www.epicgames.com->0"));
		OriginalUrl = UrlToTestMethods();
		OriginalUrl = OriginalUrl.Replace(*UrlDomain(), TEXT("www.epicgames.com"));
	}

	AltDomains.Add(CorrectDomain);
	FHttpRetrySystem::FRetryDomainsPtr RetryDomains = MakeShared<FHttpRetrySystem::FRetryDomains>(MoveTemp(AltDomains));
	TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
		1/*InRetryLimitCountOverride*/,
		FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting()/*InRetryTimeoutRelativeSecondsOverride unused*/,
		{ EHttpResponseCodes::TooManyRequests, EHttpResponseCodes::ServiceUnavail }/*InRetryResponseCodes*/,
		FHttpRetrySystem::FRetryVerbs(),
		RetryDomains
	);

	HttpRequest->SetURL(OriginalUrl);
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
	++ExpectingExtraCallbacks;
	HttpRequest->OnRequestWillRetry().BindLambda([this](FHttpRequestPtr /*Request*/, FHttpResponsePtr /*Response*/, float LockoutPeriod) {
		--ExpectingExtraCallbacks;
		CHECK(LockoutPeriod == 0);
	});
	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);
	});
	HttpRequest->ProcessRequest();

	CVarHttpUrlPatternsToMockResponse->Set(TEXT(""));
}

TEST_CASE_METHOD(FWaitUntilQuitFromTestFixture, "Will keep request reference with mock connection failure which using CompleteOnHttpThread policy until mocked connection timeout", HTTP_TAG)
{
	DisableWarningsInThisTest();

	CVarHttpUrlPatternsToMockResponse->Set(TEXT("www.epicgames.com->0"));
	HttpModule->HttpConnectionTimeout = 1.0f;

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(TEXT("https://www.epicgames.com"));
	HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
	++ExpectingExtraCallbacks;
	HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		--ExpectingExtraCallbacks;
		CHECK(!bSucceeded);
		REQUIRE(HttpResponse == nullptr);
		bQuitRequested = true;
	});
	HttpRequest->ProcessRequest();
	CVarHttpUrlPatternsToMockResponse->Set(TEXT(""));
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Retry will add default domain to alt domains", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	CVarHttpUrlPatternsToMockResponse->Set(TEXT("fakedomain->0"));

	HttpModule->HttpConnectionTimeout = 0.1f;

	TQueue<FString>* ExpectedDomainsOrderInRetry = new TQueue<FString>();
	TArray<FString> AltDomains;

	FString DefautUrl = TEXT("https://www.fakedomain-default.com/webtests/httptests");
	ExpectedDomainsOrderInRetry->Enqueue(TEXT("www.fakedomain-a.com"));
	ExpectedDomainsOrderInRetry->Enqueue(TEXT("www.fakedomain-b.com"));
	ExpectedDomainsOrderInRetry->Enqueue(TEXT("www.fakedomain-default.com"));
	ExpectedDomainsOrderInRetry->Enqueue(TEXT("www.fakedomain-a.com"));

	SECTION("When default domain is not listed in AltDomains")
	{
		AltDomains.Add(TEXT("www.fakedomain-a.com"));
		AltDomains.Add(TEXT("www.fakedomain-b.com"));
	}
	SECTION("When default domain configured as first in AltDomains, retry system will keep the order in the array intead of adding it")
	{
		AltDomains.Add(TEXT("www.fakedomain-default.com"));
		AltDomains.Add(TEXT("www.fakedomain-a.com"));
		AltDomains.Add(TEXT("www.fakedomain-b.com"));
	}
	SECTION("When default domain configured in the middle of AltDomains, retry system will move it to first in the array")
	{
		AltDomains.Add(TEXT("www.fakedomain-a.com"));
		AltDomains.Add(TEXT("www.fakedomain-default.com"));
		AltDomains.Add(TEXT("www.fakedomain-b.com"));
	}
	SECTION("When default domain configured in the end of AltDomains, retry system will move it to first in the array")
	{
		AltDomains.Add(TEXT("www.fakedomain-a.com"));
		AltDomains.Add(TEXT("www.fakedomain-b.com"));
		AltDomains.Add(TEXT("www.fakedomain-default.com"));
	}

	FHttpRetrySystem::FRetryDomainsPtr RetryDomains = MakeShared<FHttpRetrySystem::FRetryDomains>(MoveTemp(AltDomains));
	TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
		4/*InRetryLimitCountOverride*/,
		FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting()/*InRetryTimeoutRelativeSecondsOverride unused*/,
		{ EHttpResponseCodes::TooManyRequests }/*InRetryResponseCodes*/,
		FHttpRetrySystem::FRetryVerbs(),
		RetryDomains
	);

	HttpRequest->SetURL(DefautUrl);
	ExpectingExtraCallbacks = 4;
	HttpRequest->OnRequestWillRetry().BindLambda([this, ExpectedDomainsOrderInRetry](FHttpRequestPtr HttpRequest, FHttpResponsePtr /*Response*/, float LockoutPeriod) {
		--ExpectingExtraCallbacks;
		CHECK(LockoutPeriod == 0);
		FString ExpectedDomain;
		ExpectedDomainsOrderInRetry->Dequeue(ExpectedDomain);
		FString ActualDomain = FPlatformHttp::GetUrlDomain(HttpRequest->GetURL());
		CHECK(ExpectedDomain == ActualDomain);
	});
	HttpRequest->OnProcessRequestComplete().BindLambda([ExpectedDomainsOrderInRetry](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(ExpectedDomainsOrderInRetry->IsEmpty());
		delete ExpectedDomainsOrderInRetry;
		CVarHttpUrlPatternsToMockResponse->Set(TEXT(""));

		CHECK(!bSucceeded);
		REQUIRE(HttpResponse == nullptr);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Optionally retry limit can be set differently for connection error", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	HttpModule->HttpConnectionTimeout = 1.0f;

	FHttpRetrySystem::FExponentialBackoffCurve RetryBackoffCurve;
	RetryBackoffCurve.MinCoefficient = 1.0f; // no jitter

	TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
		3/*InRetryLimitCountOverride*/,
		FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting()/*InRetryTimeoutRelativeSecondsOverride unused*/,
		{ EHttpResponseCodes::TooManyRequests, EHttpResponseCodes::ServiceUnavail }/*InRetryResponseCodes*/,
		FHttpRetrySystem::FRetryVerbs(), /*unused*/
		FHttpRetrySystem::FRetryDomainsPtr(), /*unused*/
		1, /*InRetryLimitCountForConnectionErrorOverride*/
		RetryBackoffCurve/*InExponentialBackoffCurve*/
	);

	float ExpectedTimeoutDuration = 0.0f;
	float TimeDiffTolerance = 0.0f;
	SECTION("RetryLimitCountForConnectionErrorDefault:1 will be used so retries for connection error take less time")
	{
		SetInvalidDomainToTestConnectTimeout();
		HttpRequest->SetURL(UrlMockStatus(EHttpResponseCodes::TooManyRequests));

		ExpectedTimeoutDuration = 6.0f; // each request will take 1s, 1st retry back off takes 4s
		TimeDiffTolerance = 2 * UE_HTTP_CONNECTION_TIMEOUT_MAX_DEVIATION;
	}
	SECTION("RetryLimitCountDefault:3 will be used so retries in general take long")
	{
		HttpRequest->SetURL(UrlMockStatus(EHttpResponseCodes::TooManyRequests));
		HttpRequest->SetHeader(TEXT("Retry-After"), FString::Format(TEXT("{0}"), { 3 }));

		ExpectedTimeoutDuration = 9.0f; // each request will take 0s, 3 retry back offs, each back off takes 3s;
		TimeDiffTolerance = 3 * HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST;
	}

	const double StartTime = FPlatformTime::Seconds();
	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime, ExpectedTimeoutDuration, TimeDiffTolerance](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, ExpectedTimeoutDuration, TimeDiffTolerance));
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Retry fallback with exponential lock out if there is no Retry-After header", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
		2/*InRetryLimitCountOverride*/,
		FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting()/*InRetryTimeoutRelativeSecondsOverride unused*/,
		{EHttpResponseCodes::TooManyRequests}/*InRetryResponseCodes*/
	);

	HttpRequest->SetURL(UrlMockStatus(EHttpResponseCodes::TooManyRequests));
	HttpRequest->SetVerb(TEXT("GET"));

	ExpectingExtraCallbacks = 2;

	HttpRequest->OnRequestWillRetry().BindLambda([this](FHttpRequestPtr Request, FHttpResponsePtr /*Response*/, float LockoutPeriod) {
		--ExpectingExtraCallbacks;
		// Default value in FExponentialBackoffCurve Compute(1) is 4 with default value in FBackoffJitterCoefficient applied
		CHECK(LockoutPeriod >= (4 * 0.5f));
		CHECK(LockoutPeriod <= (4 * 1.0f));
		Request->OnRequestWillRetry().BindLambda([this](FHttpRequestPtr /*Request*/, FHttpResponsePtr /*Response*/, float LockoutPeriod) {
			--ExpectingExtraCallbacks;
			// Default value in FExponentialBackoffCurve Compute(2) is 8 with default value in FBackoffJitterCoefficient applied
			CHECK(LockoutPeriod >= (8 * 0.5f));
			CHECK(LockoutPeriod <= (8 * 1.0f));
		});
	});

	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Dead lock test by retrying requests while completing requests", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	for (uint32 i = 0; i < 50; ++i)
	{
		TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
			5/*InRetryLimitCountOverride*/,
			FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting()/*InRetryTimeoutRelativeSecondsOverride unused*/,
			{ EHttpResponseCodes::TooManyRequests }/*InRetryResponseCodes*/
		);

		HttpRequest->SetURL(UrlMockStatus(EHttpResponseCodes::TooManyRequests));
		HttpRequest->SetHeader(TEXT("Retry-After"), FString::Format(TEXT("{0}"), { 0.1 }));
		HttpRequest->ProcessRequest();
	}
}

class FThreadedBatchRequestsFixture : public FWaitThreadedHttpFixture
{
public:
	void LaunchBatchRequests(uint32 BatchSize)
	{
		for (uint32 i = 0; i < BatchSize; ++i)
		{
			TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
			HttpRequest->SetURL(UrlStreamDownload(3, 1024*1024));
			HttpRequest->SetVerb(TEXT("GET"));
			HttpRequest->ProcessRequest();
		}
	}

	void BlockUntilFlushed()
	{
		if (bRetryEnabled)
		{
			HttpRetryManager->BlockUntilFlushed(5.0);
		}
		else
		{
			HttpModule->GetHttpManager().Flush(EHttpFlushReason::Default);
		}
	}
};

TEST_CASE_METHOD(FThreadedBatchRequestsFixture, "Retry manager and http manager is thread safe for flushing", HTTP_TAG)
{
	DisableWarningsInThisTest();

	ThreadedHttpRunnable.OnRunFromThread().BindLambda([this]() {
		LaunchBatchRequests(10);
		BlockUntilFlushed();
	});
	ThreadedHttpRunnable.StartTestHttpThread(false/*bBlockGameThread*/);

	LaunchBatchRequests(10);
	BlockUntilFlushed();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Flush while activity timeout shouldn't dead lock", HTTP_TAG)
{
	DisableWarningsInThisTest();

	HttpModule->HttpActivityTimeout = 2.0f;

	TSharedPtr<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamDownload(3/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 5/*ChunkLatency*/));
	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::ConnectionError);
	});
	HttpRequest->ProcessRequest();

	HttpModule->GetHttpManager().Flush(EHttpFlushReason::FullFlush);
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "OnBeforeFork can clear http requests", HTTP_TAG)
{
	// Warning: Sleeping 0.500s to wait for 1 outstanding Http Requests.
	DisableWarningsInThisTest();

	{
		TSharedPtr<IHttpRequest> HttpRequest = CreateRequest();
		HttpRequest->SetURL(UrlStreamDownload(3/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 0/*ChunkLatency*/));
		HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
		});
		HttpRequest->ProcessRequest();
	}

	CHECK(OngoingRequests == 1);

	HttpModule->GetHttpManager().OnBeforeFork();

	CHECK(OngoingRequests == 0);

	HttpModule->GetHttpManager().OnAfterFork();
}

// TODO: find a way to catch expected error log instead
//TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Will trigger check if flush during http complete delegate in game thread", HTTP_TAG)
//{
//	TSharedPtr<IHttpRequest> HttpRequest = CreateRequest();
//	HttpRequest->SetURL(UrlToTestMethods());
//	HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
//		REQUIRE_CHECK_MSG("It's not supported to call Flush during http request complete delegate!", HttpModule->GetHttpManager().Flush(EHttpFlushReason::FullFlush));
//	});
//	HttpRequest->ProcessRequest();
//}
//
//TEST_CASE_METHOD(FWaitThreadedHttpFixture, "Will trigger check if flush during http complete delegate in http thread", HTTP_TAG)
//{
//	ThreadedHttpRunnable.OnRunFromThread().BindLambda([this]() {
//		TSharedPtr<IHttpRequest> HttpRequest = CreateRequest();
//		HttpRequest->SetURL(UrlToTestMethods());
//		HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
//			REQUIRE_CHECK_MSG("It's not supported to call Flush during http request complete delegate!", HttpModule->GetHttpManager().Flush(EHttpFlushReason::FullFlush));
//		});
//		HttpRequest->ProcessRequest();
//	});
//
//	ThreadedHttpRunnable.StartTestHttpThread(/*bBlockGameThread=*/false);
//}
//
//TEST_CASE_METHOD(FWaitThreadedHttpFixture, "Will NOT trigger check if flush from another thread during http complete delegate", HTTP_TAG)
//{
//	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
//	HttpRequest->SetURL(UrlToTestMethods());
//	HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
//		CHECK(bSucceeded);
//		FPlatformProcess::Sleep(1.0);
//	});
//	HttpRequest->ProcessRequest();
//
//	ThreadedHttpRunnable.OnRunFromThread().BindLambda([this]() {
//		FPlatformProcess::Sleep(0.5); // Make sure the request reached OnProcessRequestComplete
//		HttpModule->GetHttpManager().Flush(EHttpFlushReason::FullFlush);
//	});
//	ThreadedHttpRunnable.StartTestHttpThread(/*bBlockGameThread=*/false);
//}

TEST_CASE_METHOD(FWaitUntilQuitFromTestFixture, "Can disable failed log through CVar", HTTP_TAG)
{
	// Only disable warning in this function scope for pre check warning log, but not disable warning when complete
	LOG_SCOPE_VERBOSITY_OVERRIDE(LogHttp, ELogVerbosity::Error);

	CVarHttpUrlPatternsToDisableFailedLog->Set(TEXT("unrealengine.com"));

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(TEXT("https://www.unrealengine.com/"));
	HttpRequest->SetVerb(TEXT("")); // Pre check will fail
	// Make sure when complete it's not in this function scope
	HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);

	HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		bQuitRequested = true;
		CHECK(!bSucceeded);
		HttpModule->GetHttpManager().AddGameThreadTask([]()
		{
			// Have to clear CVar out of the test function scope, but also need to be in game thread.
			CVarHttpUrlPatternsToDisableFailedLog->Set(TEXT(""));
		});
	});
	HttpRequest->ProcessRequest();
}

#if UE_HTTP_SUPPORT_LOCAL_SERVER

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Scheme besides http and https can work if allowed by settings", HTTP_TAG)
{
	bool bShouldSucceed = false;
	SECTION("when allowed")
	{
		bShouldSucceed = true;
	}
	SECTION("when not allowed")
	{
		DisableWarningsInThisTest();
		// Pre check will fail when scheme is not listed
		UE::TestHttp::SetupURLRequestFilter(HttpModule);
	}

	FString Filename = FString(FPlatformProcess::UserSettingsDir()) / TEXT("TestProtocolAllowed.dat");
	UE::TestHttp::WriteTestFile(Filename, 10/*Bytes*/);

	TSharedRef<IHttpRequest> HttpRequest = HttpModule->CreateRequest();
	HttpRequest->SetURL(FString(TEXT("file://")) + Filename.Replace(TEXT(" "), TEXT("%20")));
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->OnProcessRequestComplete().BindLambda([Filename, bShouldSucceed](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded == bShouldSucceed);
		IFileManager::Get().Delete(*Filename);
	});
	HttpRequest->ProcessRequest();
}

extern TAutoConsoleVariable<bool> CVarHttpServerDefaultReuseAddressAndPortEnabled;

class FLocalHttpServerFixture : public FWaitUntilCompleteHttpFixture
{
public:
	FLocalHttpServerFixture()
	{
		// It could fail to bind the same port between test cases, when previous test is still cleaning up on OS layer
		// So make sure it can reuse the same address and port
		CVarHttpServerDefaultReuseAddressAndPortEnabled->Set(true);

		HttpServerModule = new FHttpServerModule();
		HttpServerModule->StartupModule();

		HttpRouter = HttpServerModule->GetHttpRouter(LocalHttpServerPort);
		CHECK(HttpRouter.IsValid());
	}

	void StartServerWithHandler(const FHttpPath& HttpPath, EHttpServerRequestVerbs Verb, FHttpRequestHandler RequestHandler)
	{
		CHECK(HttpRouteHandle == nullptr);
		HttpRouteHandle = HttpRouter->BindRoute(HttpPath, Verb, RequestHandler);
		HttpServerModule->StartAllListeners();
	}

	~FLocalHttpServerFixture()
	{
		while (HasOngoingRequest())
		{
			HttpServerModule->Tick(TickFrequency);
			HttpModule->GetHttpManager().Tick(TickFrequency);
			FPlatformProcess::Sleep(TickFrequency);
		}

		HttpRouter->UnbindRoute(HttpRouteHandle);
		HttpRouter.Reset();

		HttpServerModule->ShutdownModule();
		delete HttpServerModule;
	}

	TSharedPtr<IHttpRouter> HttpRouter;
	FHttpRouteHandle HttpRouteHandle;
	FHttpServerModule* HttpServerModule = nullptr;
	uint32 LocalHttpServerPort = 9000;
};

TEST_CASE_METHOD(FLocalHttpServerFixture, "Local http server can serve large file", HTTP_TAG)
{
	const uint32 FileSize = 100 * 1024 * 1024; // 100 MB seems good enough to repro SE_EWOULDBLOCK or SE_TRY_AGAIN on Mac
	StartServerWithHandler(FHttpPath(TEXT("/large_file")), EHttpServerRequestVerbs::VERB_GET, FHttpRequestHandler::CreateLambda([FileSize](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) {
		TArray<uint8> ResultData;
		ResultData.SetNum(FileSize);
		FMemory::Memset(ResultData.GetData(), 'd', FileSize);

		OnComplete(FHttpServerResponse::Create(MoveTemp(ResultData), TEXT("text/text")));
		return true;
	}));

	// Start client request
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(FString::Format(TEXT("http://localhost:{0}/large_file"), {LocalHttpServerPort}));
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->OnProcessRequestComplete().BindLambda([FileSize](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetContentLength() == FileSize);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FLocalHttpServerFixture, "Local http server can handle post request", HTTP_TAG)
{
	StartServerWithHandler(FHttpPath(TEXT("/post_path")), EHttpServerRequestVerbs::VERB_POST, FHttpRequestHandler::CreateLambda([](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) {
		FString JsonString;
		TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonString);
		JsonWriter->WriteObjectStart();
		JsonWriter->WriteValue("success", false);
		JsonWriter->WriteObjectEnd();
		JsonWriter->Close();

		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(JsonString, TEXT("application/json"));
		OnComplete(MoveTemp(Response));

		return true;
	}));

	uint32 ChunkSize = 128;
	TArray<uint8> DataChunk;
	DataChunk.SetNum(ChunkSize);
	FMemory::Memset(DataChunk.GetData(), 'd', ChunkSize);

	// Start client request
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(FString::Format(TEXT("http://localhost:{0}/post_path"), {LocalHttpServerPort}));
	HttpRequest->SetVerb(FHttpConstants::VERB_POST);
	HttpRequest->SetContent(DataChunk);
	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
	});
	HttpRequest->ProcessRequest();
}

// ---------------------------------------------------------------------------
// SSE / Streaming Tests — live HTTP server over a raw TCP client
//
// Exercise the three streaming flag combinations end-to-end against a real
// FHttpServerModule. IHttpRequest buffers the full response body, so we use
// a raw TCP socket client to observe mid-stream frames on the wire.
// ---------------------------------------------------------------------------

namespace SseTestHelpers
{
	static FSocket* ConnectClient(ISocketSubsystem* SocketSubsystem, uint32 Port)
	{
		FSocket* ClientSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("SSETestClient"), false);
		if (!ClientSocket)
		{
			return nullptr;
		}

		TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
		Addr->SetIp(0x7F000001); // 127.0.0.1
		Addr->SetPort(Port);
		if (!ClientSocket->Connect(*Addr))
		{
			SocketSubsystem->DestroySocket(ClientSocket);
			return nullptr;
		}
		ClientSocket->SetNonBlocking(true);
		return ClientSocket;
	}

	static void SendRequest(FSocket* Socket, const FString& Request)
	{
		FTCHARToUTF8 Utf8(*Request);
		int32 BytesSent = 0;
		Socket->Send(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), BytesSent);
	}

	static FString ReadAvailable(FSocket* Socket, float WaitSeconds)
	{
		Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(WaitSeconds));

		TArray<uint8> Buffer;
		uint8 ReadBuf[4096];
		int32 BytesRead = 0;
		while (Socket->Recv(ReadBuf, sizeof(ReadBuf), BytesRead))
		{
			if (BytesRead <= 0)
			{
				break;
			}
			Buffer.Append(ReadBuf, BytesRead);
		}
		FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Buffer.GetData()), Buffer.Num());
		return FString(Converter.Length(), Converter.Get());
	}

	// Tick the server until the predicate returns true or the iteration cap is hit.
	template <typename PredicateType>
	static void TickUntil(FHttpServerModule* Module, float TickFrequency, int32 MaxIterations, PredicateType&& Predicate)
	{
		for (int32 i = 0; i < MaxIterations && !Predicate(); ++i)
		{
			Module->Tick(TickFrequency);
			FPlatformProcess::Sleep(TickFrequency);
		}
	}
}

TEST_CASE_METHOD(FLocalHttpServerFixture, "Local http server SSE open emits headers without content-length", HTTP_TAG)
{
	using namespace SseTestHelpers;

	// Stage 1 pattern: MultipleWriteStream | HasAdditionalWrites with custom SSE headers and empty body.
	StartServerWithHandler(FHttpPath(TEXT("/sse_open")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) {
			TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(FString(TEXT("")), TEXT("text/event-stream"));
			Response->Code = EHttpServerResponseCodes::Ok;
			Response->HttpVersion = HttpVersion::EHttpServerHttpVersion::HTTP_VERSION_1_1;
			Response->Headers.Add(TEXT("Connection"), { TEXT("keep-alive") });
			Response->Headers.Add(TEXT("Cache-Control"), { TEXT("no-cache") });
			Response->Flags = EHttpServerResponseFlags::MultipleWriteStream | EHttpServerResponseFlags::HasAdditionalWrites;
			OnComplete(MoveTemp(Response));
			return true;
		}));

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	REQUIRE(SocketSubsystem != nullptr);
	FSocket* ClientSocket = ConnectClient(SocketSubsystem, LocalHttpServerPort);
	REQUIRE(ClientSocket != nullptr);
	ON_SCOPE_EXIT{ SocketSubsystem->DestroySocket(ClientSocket); };

	SendRequest(ClientSocket, TEXT("GET /sse_open HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n"));

	FString Output;
	TickUntil(HttpServerModule, TickFrequency, 600, [&]() {
		Output += ReadAvailable(ClientSocket, 0.01f);
		return Output.Contains(TEXT("\r\n\r\n"));
	});

	CHECK(Output.StartsWith(TEXT("HTTP/1.1 200")));
	CHECK(Output.Contains(TEXT("content-type: text/event-stream")));
	CHECK(Output.Contains(TEXT("connection: keep-alive")));
	CHECK(Output.Contains(TEXT("cache-control: no-cache")));
	CHECK(!Output.Contains(TEXT("content-length")));
}

TEST_CASE_METHOD(FLocalHttpServerFixture, "Local http server SSE mid-stream frame omits headers", HTTP_TAG)
{
	using namespace SseTestHelpers;

	TSharedPtr<FHttpResultCallback> StashedCallback = MakeShared<FHttpResultCallback>();

	StartServerWithHandler(FHttpPath(TEXT("/sse_mid")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([StashedCallback](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) {
			*StashedCallback = OnComplete;

			TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(FString(TEXT("")), TEXT("text/event-stream"));
			Response->Code = EHttpServerResponseCodes::Ok;
			Response->HttpVersion = HttpVersion::EHttpServerHttpVersion::HTTP_VERSION_1_1;
			Response->Flags = EHttpServerResponseFlags::MultipleWriteStream | EHttpServerResponseFlags::HasAdditionalWrites;
			OnComplete(MoveTemp(Response));
			return true;
		}));

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	REQUIRE(SocketSubsystem != nullptr);
	FSocket* ClientSocket = ConnectClient(SocketSubsystem, LocalHttpServerPort);
	REQUIRE(ClientSocket != nullptr);
	ON_SCOPE_EXIT{ SocketSubsystem->DestroySocket(ClientSocket); };

	SendRequest(ClientSocket, TEXT("GET /sse_mid HTTP/1.1\r\nHost: localhost\r\n\r\n"));

	// Wait until the open-stream headers are on the wire and the handler has stashed OnComplete.
	FString OpenOutput;
	TickUntil(HttpServerModule, TickFrequency, 600, [&]() {
		OpenOutput += ReadAvailable(ClientSocket, 0.01f);
		return OpenOutput.Contains(TEXT("\r\n\r\n")) && static_cast<bool>(*StashedCallback);
	});
	REQUIRE(static_cast<bool>(*StashedCallback));
	REQUIRE(OpenOutput.Contains(TEXT("HTTP/1.1 200")));

	// Emit a mid-stream frame: MWS|HAW|SkipHeaderWrite, body only, no HTTP status line on the wire.
	{
		TUniquePtr<FHttpServerResponse> Mid = MakeUnique<FHttpServerResponse>();
		Mid->Code = EHttpServerResponseCodes::Ok;
		Mid->HttpVersion = HttpVersion::EHttpServerHttpVersion::HTTP_VERSION_1_1;
		Mid->Flags = EHttpServerResponseFlags::MultipleWriteStream | EHttpServerResponseFlags::HasAdditionalWrites | EHttpServerResponseFlags::SkipHeaderWrite;
		const FString Chunk = TEXT("data: mid-chunk\n\n");
		FTCHARToUTF8 Utf8(*Chunk);
		Mid->Body.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		(*StashedCallback)(MoveTemp(Mid));
	}

	FString MidOutput;
	TickUntil(HttpServerModule, TickFrequency, 600, [&]() {
		MidOutput += ReadAvailable(ClientSocket, 0.01f);
		return MidOutput.Contains(TEXT("mid-chunk"));
	});
	CHECK(!MidOutput.StartsWith(TEXT("HTTP/")));
	CHECK(!MidOutput.Contains(TEXT("content-length")));
	CHECK(MidOutput.Contains(TEXT("data: mid-chunk")));
}

TEST_CASE_METHOD(FLocalHttpServerFixture, "Local http server SSE final frame closes stream without HAW", HTTP_TAG)
{
	using namespace SseTestHelpers;

	TSharedPtr<FHttpResultCallback> StashedCallback = MakeShared<FHttpResultCallback>();

	StartServerWithHandler(FHttpPath(TEXT("/sse_final")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([StashedCallback](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) {
			*StashedCallback = OnComplete;

			TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(FString(TEXT("")), TEXT("text/event-stream"));
			Response->Code = EHttpServerResponseCodes::Ok;
			Response->HttpVersion = HttpVersion::EHttpServerHttpVersion::HTTP_VERSION_1_1;
			Response->Flags = EHttpServerResponseFlags::MultipleWriteStream | EHttpServerResponseFlags::HasAdditionalWrites;
			OnComplete(MoveTemp(Response));
			return true;
		}));

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	REQUIRE(SocketSubsystem != nullptr);
	FSocket* ClientSocket = ConnectClient(SocketSubsystem, LocalHttpServerPort);
	REQUIRE(ClientSocket != nullptr);
	ON_SCOPE_EXIT{ SocketSubsystem->DestroySocket(ClientSocket); };

	SendRequest(ClientSocket, TEXT("GET /sse_final HTTP/1.1\r\nHost: localhost\r\n\r\n"));

	FString OpenOutput;
	TickUntil(HttpServerModule, TickFrequency, 600, [&]() {
		OpenOutput += ReadAvailable(ClientSocket, 0.01f);
		return OpenOutput.Contains(TEXT("\r\n\r\n")) && static_cast<bool>(*StashedCallback);
	});
	REQUIRE(static_cast<bool>(*StashedCallback));

	// Emit final frame: MWS|SkipHeaderWrite without HasAdditionalWrites.
	{
		TUniquePtr<FHttpServerResponse> Final = MakeUnique<FHttpServerResponse>();
		Final->Code = EHttpServerResponseCodes::Ok;
		Final->HttpVersion = HttpVersion::EHttpServerHttpVersion::HTTP_VERSION_1_1;
		Final->Flags = EHttpServerResponseFlags::MultipleWriteStream | EHttpServerResponseFlags::SkipHeaderWrite;
		const FString Chunk = TEXT("data: final-chunk\n\n");
		FTCHARToUTF8 Utf8(*Chunk);
		Final->Body.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		(*StashedCallback)(MoveTemp(Final));
	}

	FString FinalOutput;
	TickUntil(HttpServerModule, TickFrequency, 600, [&]() {
		FinalOutput += ReadAvailable(ClientSocket, 0.01f);
		return FinalOutput.Contains(TEXT("final-chunk"));
	});
	CHECK(!FinalOutput.StartsWith(TEXT("HTTP/")));
	CHECK(FinalOutput.Contains(TEXT("data: final-chunk")));
}

TEST_CASE_METHOD(FLocalHttpServerFixture, "Local http server SSE callback re-invocation delivers full lifecycle", HTTP_TAG)
{
	using namespace SseTestHelpers;

	TSharedPtr<FHttpResultCallback> StashedCallback = MakeShared<FHttpResultCallback>();

	StartServerWithHandler(FHttpPath(TEXT("/sse_full")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([StashedCallback](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) {
			*StashedCallback = OnComplete;

			TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(FString(TEXT("")), TEXT("text/event-stream"));
			Response->Code = EHttpServerResponseCodes::Ok;
			Response->HttpVersion = HttpVersion::EHttpServerHttpVersion::HTTP_VERSION_1_1;
			Response->Headers.Add(TEXT("Connection"), { TEXT("keep-alive") });
			Response->Headers.Add(TEXT("Cache-Control"), { TEXT("no-cache") });
			Response->Flags = EHttpServerResponseFlags::MultipleWriteStream | EHttpServerResponseFlags::HasAdditionalWrites;
			OnComplete(MoveTemp(Response));
			return true;
		}));

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	REQUIRE(SocketSubsystem != nullptr);
	FSocket* ClientSocket = ConnectClient(SocketSubsystem, LocalHttpServerPort);
	REQUIRE(ClientSocket != nullptr);
	ON_SCOPE_EXIT{ SocketSubsystem->DestroySocket(ClientSocket); };

	SendRequest(ClientSocket, TEXT("GET /sse_full HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n"));

	FString Accumulated;

	// Stage 1: open
	TickUntil(HttpServerModule, TickFrequency, 600, [&]() {
		Accumulated += ReadAvailable(ClientSocket, 0.01f);
		return Accumulated.Contains(TEXT("\r\n\r\n")) && static_cast<bool>(*StashedCallback);
	});
	REQUIRE(static_cast<bool>(*StashedCallback));
	CHECK(Accumulated.StartsWith(TEXT("HTTP/1.1 200")));
	CHECK(Accumulated.Contains(TEXT("content-type: text/event-stream")));
	CHECK(!Accumulated.Contains(TEXT("content-length")));

	// Stage 2: two mid-stream frames via the same callback, back-to-back.
	auto EmitMidFrame = [&](const TCHAR* Payload) {
		TUniquePtr<FHttpServerResponse> Mid = MakeUnique<FHttpServerResponse>();
		Mid->Code = EHttpServerResponseCodes::Ok;
		Mid->HttpVersion = HttpVersion::EHttpServerHttpVersion::HTTP_VERSION_1_1;
		Mid->Flags = EHttpServerResponseFlags::MultipleWriteStream | EHttpServerResponseFlags::HasAdditionalWrites | EHttpServerResponseFlags::SkipHeaderWrite;
		FTCHARToUTF8 Utf8(Payload);
		Mid->Body.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		(*StashedCallback)(MoveTemp(Mid));
	};

	EmitMidFrame(TEXT("data: first\n\n"));
	TickUntil(HttpServerModule, TickFrequency, 600, [&]() {
		Accumulated += ReadAvailable(ClientSocket, 0.01f);
		return Accumulated.Contains(TEXT("data: first"));
	});

	EmitMidFrame(TEXT("data: second\n\n"));
	TickUntil(HttpServerModule, TickFrequency, 600, [&]() {
		Accumulated += ReadAvailable(ClientSocket, 0.01f);
		return Accumulated.Contains(TEXT("data: second"));
	});

	// Stage 3: final frame closes the stream (no HAW → connection returns to AwaitingRead after write).
	{
		TUniquePtr<FHttpServerResponse> Final = MakeUnique<FHttpServerResponse>();
		Final->Code = EHttpServerResponseCodes::Ok;
		Final->HttpVersion = HttpVersion::EHttpServerHttpVersion::HTTP_VERSION_1_1;
		Final->Flags = EHttpServerResponseFlags::MultipleWriteStream | EHttpServerResponseFlags::SkipHeaderWrite;
		const FString Chunk = TEXT("data: final\n\n");
		FTCHARToUTF8 Utf8(*Chunk);
		Final->Body.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		(*StashedCallback)(MoveTemp(Final));
	}

	TickUntil(HttpServerModule, TickFrequency, 600, [&]() {
		Accumulated += ReadAvailable(ClientSocket, 0.01f);
		return Accumulated.Contains(TEXT("data: final"));
	});

	// All three payloads must be present, in order, in a single HTTP response prelude.
	const int32 FirstIdx = Accumulated.Find(TEXT("data: first"));
	const int32 SecondIdx = Accumulated.Find(TEXT("data: second"));
	const int32 FinalIdx = Accumulated.Find(TEXT("data: final"));
	CHECK(FirstIdx != INDEX_NONE);
	CHECK(SecondIdx > FirstIdx);
	CHECK(FinalIdx > SecondIdx);
}

// Regression for UE-380078: a handler that invokes `OnComplete` twice on the same stack while the
// first response is still mid-`Writing` (partial socket drain) used to assert
// `check(AwaitingProcessing == State)` in `FHttpConnection::CompleteRead`. The second response is
// now captured into the connection's single-slot pending response and drained by `CompleteWrite`
// once the in-flight write finishes; both frames reach the wire.
//
// To hold the connection in `Writing` deterministically without relying on OS-specific TCP buffer
// behaviour, the first response is given a valid but empty `StreamingBodyQueue`. `WriteStream`
// defers header serialization (`bStreamingHeadersPending`) and returns `Continue`, so the
// connection sits in `Writing` from the moment `BeginWrite` runs synchronously inside the first
// `OnComplete`.
TEST_CASE_METHOD(FLocalHttpServerFixture, "Local http server queues a synchronous OnComplete that arrives while a previous response is mid-Writing", HTTP_TAG)
{
	using namespace SseTestHelpers;

	// Captured outside the handler so the test body can unblock the first response after the
	// reentry has happened (enqueue a chunk + signal completion).
	TSharedPtr<TQueue<TArray<uint8>, EQueueMode::Spsc>> FirstStreamingBodyQueue =
		MakeShared<TQueue<TArray<uint8>, EQueueMode::Spsc>>();
	TSharedPtr<TAtomic<bool>> FirstStreamingBodyComplete = MakeShared<TAtomic<bool>>(false);

	std::atomic<bool> bFirstOnCompleteReturned = false;
	std::atomic<bool> bSecondOnCompleteReturned = false;

	StartServerWithHandler(FHttpPath(TEXT("/sse_reentry_during_write")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda(
			[&bFirstOnCompleteReturned, &bSecondOnCompleteReturned, FirstStreamingBodyQueue, FirstStreamingBodyComplete]
			(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) -> bool
			{
				// Stage 1: open the SSE stream with an empty `StreamingBodyQueue`. `WriteStream` sees
				// `bStreamingHeadersPending` and an empty queue, so it returns `Continue` without
				// touching the socket - the connection sits in `Writing` waiting for chunks.
				TUniquePtr<FHttpServerResponse> First = MakeUnique<FHttpServerResponse>();
				First->Code = EHttpServerResponseCodes::Ok;
				First->HttpVersion = HttpVersion::EHttpServerHttpVersion::HTTP_VERSION_1_1;
				First->Headers.Add(TEXT("Content-Type"), { TEXT("text/event-stream") });
				First->Headers.Add(TEXT("Cache-Control"), { TEXT("no-cache") });
				First->Flags = EHttpServerResponseFlags::MultipleWriteStream | EHttpServerResponseFlags::HasAdditionalWrites;
				First->StreamingBodyQueue = FirstStreamingBodyQueue;
				First->StreamingBodyComplete = FirstStreamingBodyComplete;
				OnComplete(MoveTemp(First));
				bFirstOnCompleteReturned = true;

				// Stage 2: emit on the same call stack. The connection is held in `Writing` by the
				// empty-queue stall on the first response, so this exercises the pending-slot
				// capture and `CompleteWrite` drain.
				TUniquePtr<FHttpServerResponse> Second = MakeUnique<FHttpServerResponse>();
				Second->Code = EHttpServerResponseCodes::Ok;
				Second->HttpVersion = HttpVersion::EHttpServerHttpVersion::HTTP_VERSION_1_1;
				Second->Flags = EHttpServerResponseFlags::MultipleWriteStream
					| EHttpServerResponseFlags::HasAdditionalWrites
					| EHttpServerResponseFlags::SkipHeaderWrite;
				const FString Chunk = TEXT("data: tool-result\n\n");
				FTCHARToUTF8 Utf8(*Chunk);
				Second->Body.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
				OnComplete(MoveTemp(Second));
				bSecondOnCompleteReturned = true;

				return true;
			}));

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	REQUIRE(SocketSubsystem != nullptr);
	FSocket* ClientSocket = ConnectClient(SocketSubsystem, LocalHttpServerPort);
	REQUIRE(ClientSocket != nullptr);
	ON_SCOPE_EXIT{ SocketSubsystem->DestroySocket(ClientSocket); };

	SendRequest(ClientSocket, TEXT("GET /sse_reentry_during_write HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n"));

	// Tick until the handler has invoked OnComplete twice on the same stack.
	TickUntil(HttpServerModule, TickFrequency, 600, [&]() { return bSecondOnCompleteReturned.load(); });
	REQUIRE(bSecondOnCompleteReturned.load());
	CHECK(bFirstOnCompleteReturned.load());

	// Unblock the first response: enqueue a marker chunk and signal end-of-stream. With the fix
	// in place the queued second response then drains naturally through `CompleteWrite`.
	const FString FirstChunkText = TEXT("data: first-frame\n\n");
	FTCHARToUTF8 FirstUtf8(*FirstChunkText);
	TArray<uint8> FirstChunkBytes;
	FirstChunkBytes.Append(reinterpret_cast<const uint8*>(FirstUtf8.Get()), FirstUtf8.Length());
	FirstStreamingBodyQueue->Enqueue(MoveTemp(FirstChunkBytes));
	FirstStreamingBodyComplete->Store(true);

	// Both frames must reach the wire. A regression that silently drops the pending response would
	// deliver only the first frame and this poll would time out.
	FString Output;
	TickUntil(HttpServerModule, TickFrequency, 600, [&]() {
		Output += ReadAvailable(ClientSocket, 0.01f);
		return Output.Contains(TEXT("data: first-frame")) && Output.Contains(TEXT("data: tool-result"));
	});
	CHECK(Output.Contains(TEXT("data: first-frame")));
	CHECK(Output.Contains(TEXT("data: tool-result")));
}

#endif // UE_HTTP_SUPPORT_LOCAL_SERVER

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Accessing request initial information without issue while request is running", HTTP_TAG)
{
	for (int32 i = 0; i < 30; ++i) // Use 2 "for" loops so it doesn't trigger the warning the request waited for too long in the queue
	{
		TArray<TSharedRef<IHttpRequest>> Requests;
		for (int32 j = 0; j < 30; ++j)
		{
			TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
			HttpRequest->SetHeader(TEXT("Custom-HeaderA"), TEXT("a"));
			HttpRequest->SetHeader(TEXT("Custom-HeaderB"), TEXT("b"));
			HttpRequest->SetHeader(TEXT("Custom-HeaderC"), TEXT("c"));
			HttpRequest->SetURL(UrlStreamDownload(3, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 0));
			HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
			HttpRequest->ProcessRequest();
			Requests.Add(HttpRequest);
		}

		bool bRequestsStillRunning = true;
		while (bRequestsStillRunning)
		{
			bRequestsStillRunning = false;
			for (TSharedRef<IHttpRequest> Request : Requests)
			{
				if (!EHttpRequestStatus::IsFinished(Request->GetStatus()))
				{
					bRequestsStillRunning = true;

					CHECK(!Request->GetAllHeaders().IsEmpty());
					CHECK(!Request->GetURL().IsEmpty());
				}
			}
		}
	}
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Test platform request requests limits", HTTP_TAG "[LIMIT]")
{
	bool bCheckCancel = GENERATE(false, true);
	int32 NumRequests = GENERATE(1, 10, 20, 50, 100, 200, 500, 1000);
#if UE_HTTP_SUPPORT_OPTION_REUSE_CONNECTION
	bool bChangeReuseConnection = GENERATE(false, true);
#endif
	//Output NumRequests when error occurs.
	UNSCOPED_INFO(NumRequests);
	UNSCOPED_INFO(bCheckCancel);

	DYNAMIC_SECTION(" making " << NumRequests << " requests with bCheckCancel=" << bCheckCancel)
	{
		if (NumRequests > 50 && !bRunHeavyTests)
		{
			return;
		}

		TArray<TSharedRef<IHttpRequest>> Requests;

		for (int32 i = 0; i < NumRequests; ++i)
		{
			TSharedRef<IHttpRequest> HttpRequest = FHttpModule::Get().CreateRequest();
			// Requests server to serve 1024b chunks to allow time for cancel to happen
			HttpRequest->SetURL(UrlStreamDownload(3, HTTP_TEST_TIMEOUT_CHUNK_SIZE, /*ChunkLatency=*/bCheckCancel ? 1 : 0));
			HttpRequest->SetVerb(TEXT("GET"));

			// Since catch2 uses std::srand, use std::rand here should make it deterministic when use same seed through --rng-seed
			if (std::rand() % 2)
			{
				HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
			}

			HttpRequest->OnProcessRequestComplete().BindLambda([bCheckCancel](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
			{
				//Only assert if response is successful on non-canceled requests
				if (!bCheckCancel)
				{
					CHECK(bSucceeded);
					CHECK(HttpResponse != nullptr);
				}
			});
			HttpRequest->ProcessRequest();

			Requests.Add(HttpRequest);
		}

		CHECK(Requests.Num() == NumRequests);

		if(bCheckCancel)
		{
			// Make sure requests are started in http thread
			FPlatformProcess::Sleep(0.1f);

			for (auto Request : Requests)
			{
				Request->CancelRequest();
			}
		}

#if UE_HTTP_SUPPORT_OPTION_REUSE_CONNECTION
		if (bChangeReuseConnection)
		{
			// Make sure requests are running in http thread with random delay (0.1s to 10s)
			FPlatformProcess::Sleep(FMath::FRandRange(0.1f, 10.0f));

			const bool bOriginalValue = CVarHttpReuseConnectionEnabled.GetValueOnAnyThread();
			CVarHttpReuseConnectionEnabled->Set(!bOriginalValue);

			// Let requests continue running with the new setting with random delay (0.1s to 10s)
			FPlatformProcess::Sleep(FMath::FRandRange(0.1f, 10.0f));

			CVarHttpReuseConnectionEnabled->Set(bOriginalValue);
		}
#endif
	}
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "RecordStatRequest increments RequestResults for None and ConnectionError", HTTP_TAG)
{
	DisableWarningsInThisTest();

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();

	SECTION("Succeeded request increments RequestResults[None]")
	{
		HttpRequest->SetURL(UrlToTestMethods());
		HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
		{
			CHECK(bSucceeded);
			const int64 NoneCountAfter = HttpModule->GetHttpManager().GetHttpStats().RequestResults[static_cast<uint8>(EHttpFailureReason::None)].load();
			CHECK(NoneCountAfter == 1);
		});
	}

	SECTION("Failed request with ConnectionError increments RequestResults[ConnectionError]")
	{
		HttpModule->HttpConnectionTimeout = 0.1f;
		CVarHttpUrlPatternsToMockResponse->Set(TEXT("www.epicgames.com->0"));

		HttpRequest->SetURL(TEXT("https://www.epicgames.com"));
		HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
		{
			CHECK(!bSucceeded);
			CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::ConnectionError);
			const int64 ConnectionErrorCountAfter = HttpModule->GetHttpManager().GetHttpStats().RequestResults[static_cast<uint8>(EHttpFailureReason::ConnectionError)].load();
			CHECK(ConnectionErrorCountAfter == 1);
			CVarHttpUrlPatternsToMockResponse->Set(TEXT(""));
		});
	}

	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "POST body is received correctly by the server", HTTP_TAG)
{
	struct FEchoBodyResponse : public FJsonSerializable
	{
		FString WorkspaceId;
		FString UxTraceId;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE("workspaceId", WorkspaceId);
			JSON_SERIALIZE("uxTraceId", UxTraceId);
		END_JSON_SERIALIZER
	};

	const FString RequestBody = TEXT(R"({
		"workspaceId":"default",
		"uxTraceId":"64dd71404524bc1e868d8787ee2be934"
	})");

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlPostEchoBody());
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetContentAsString(RequestBody);
	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
	{
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);

		FEchoBodyResponse Response;
		REQUIRE(Response.FromJsonStringView(HttpResponse->GetContentAsUtf8StringView()));
		CHECK(Response.WorkspaceId == TEXT("default"));
		CHECK(Response.UxTraceId == TEXT("64dd71404524bc1e868d8787ee2be934"));
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FHttpModuleTestFixture, "RetryHttpRequestWithDelay called during Flush does not crash", HTTP_TAG)
{
	// Regression test for EXCEPTION_ACCESS_VIOLATION in FHttpRetrySystem::FManager::RetryHttpRequestWithDelay
	// during engine shutdown (tick [0]). The crash occurs when a retry request's connection error completion
	// fires during FHttpManager::Flush(Shutdown) and calls AddHttpThreadTask on the still-alive Thread.
	//
	// Setup: start a retry request against a non-routable IP so it fails with a connection error quickly.
	// Then immediately call ShutdownModule() — the request completes during Flush, RetryHttpRequestWithDelay
	// is called, and AddHttpThreadTask must not crash.
	//
	// Use near-zero backoff so that the queued event-loop timer fires almost immediately, preventing
	// StopThread() from hanging while waiting for the event loop to drain.
	if (!bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	// Short connection timeout so the request fails quickly during Flush.
	// GetHttpConnectionTimeout() returns int32, so use at least 1 (0.5f truncates to 0 which check-fails).
	HttpModule->HttpConnectionTimeout = 1.0f;

	// Near-zero backoff: Compute(1) = Base^(1+1) * Coeff = 1.001^2 * ~0 ≈ 0
	// This ensures StopThread() doesn't block waiting for a multi-second timer to drain.
	FHttpRetrySystem::FExponentialBackoffCurve RetryBackoffCurve;
	RetryBackoffCurve.Base = 1.001f;
	RetryBackoffCurve.MinCoefficient = 0.0f;
	RetryBackoffCurve.MaxCoefficient = 0.001f;

	TSharedPtr<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
		1/*InRetryLimitCountOverride*/,
		FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting(),
		{ EHttpResponseCodes::TooManyRequests, EHttpResponseCodes::ServiceUnavail },
		FHttpRetrySystem::FRetryVerbs(),
		FHttpRetrySystem::FRetryDomainsPtr(),
		1/*InRetryLimitCountForConnectionErrorOverride*/,
		RetryBackoffCurve
	);

	SetInvalidDomainToTestConnectTimeout();
	HttpRequest->SetURL(UrlToTestMethods());
	HttpRequest->SetVerb(TEXT("GET"));

	HttpRequest->ProcessRequest();

	// Shut down immediately — the request is still in-flight.
	// During Flush(Shutdown), the connection times out, HttpOnProcessRequestComplete fires,
	// RetryHttpRequestWithDelay calls AddHttpThreadTask. This must not crash.
	ShutdownModule();
	HttpRequest.Reset();
}

TEST_CASE_METHOD(FHttpModuleTestFixture, "Shutdown http module while request is in back-off delay after connection error", HTTP_TAG)
{
	// Regression test for shutdown during back-off after connection error.
	// Verifies that FManager destruction while a delayed retry task is pending doesn't crash,
	// particularly when CVarHttpCancelSupportDuringBackOffEnabled=true causes ClearInCaseOfRetry
	// to be called before queuing the delayed lambda.
	if (!bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	HttpModule->HttpConnectionTimeout = 1.0f;

	FHttpRetrySystem::FExponentialBackoffCurve RetryBackoffCurve;
	RetryBackoffCurve.MinCoefficient = 1.0f; // no jitter, default backoff will be ~4s

	TSharedPtr<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
		1/*InRetryLimitCountOverride*/,
		FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting(),
		{ EHttpResponseCodes::TooManyRequests, EHttpResponseCodes::ServiceUnavail },
		FHttpRetrySystem::FRetryVerbs(),
		FHttpRetrySystem::FRetryDomainsPtr(),
		1/*InRetryLimitCountForConnectionErrorOverride*/,
		RetryBackoffCurve
	);

	SetInvalidDomainToTestConnectTimeout();
	HttpRequest->SetURL(UrlToTestMethods());
	HttpRequest->SetVerb(TEXT("GET"));

	std::atomic<bool> bWillRetryFired = false;
	HttpRequest->OnRequestWillRetry().BindLambda([&bWillRetryFired](FHttpRequestPtr /*Request*/, FHttpResponsePtr /*Response*/, float /*LockoutPeriod*/) {
		bWillRetryFired = true;
		// Do NOT cancel — let the delayed task get queued, then shut down while it's pending
	});

	HttpRequest->ProcessRequest();

	// Tick until WillRetry fires (meaning ClearInCaseOfRetry has been called and delayed task queued)
	const double StartTime = FPlatformTime::Seconds();
	double LastTick = StartTime;
	while (!bWillRetryFired)
	{
		const double Now = FPlatformTime::Seconds();
		HttpModule->GetHttpManager().Tick(Now - LastTick);
		LastTick = Now;
		FPlatformProcess::Sleep(1.0f / 60);
		if (Now - StartTime > 10.0)
		{
			break;
		}
	}

	CHECK(bWillRetryFired);

	// Shut down while the delayed retry task is still pending (backoff ~4s not elapsed yet).
	// HttpRequest is still alive here, so its destructor will fire after the platform HTTP library is shut down.
	ShutdownModule();
	HttpRequest.Reset(); // release our ref; destructor fires here with platform HTTP library already shut down
}

#if UE_HTTP_SUPPORT_UNIX_SOCKET

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http Methods over Unix Domain Socket", HTTP_TAG)
{
	if (WebServerUnixSocket.Len() == 0)
	{
		return;
	}

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	CHECK(HttpRequest->GetVerb() == TEXT("GET"));

	const int Number = FPlatformTime::Cycles();

	HttpRequest->SetURL(FString::Format(TEXT("{0}/{1}"), { *UrlUnixSocketHttpTests(), Number }));
	HttpRequest->SetOption(HttpRequestOptions::UnixSocketPath, WebServerUnixSocket);

	SECTION("Default GET")
	{
	}
	SECTION("GET")
	{
		HttpRequest->SetVerb(TEXT("GET"));
	}
	SECTION("POST")
	{
		HttpRequest->SetVerb(TEXT("POST"));
	}
	SECTION("PUT")
	{
		HttpRequest->SetVerb(TEXT("PUT"));
	}
	SECTION("DELETE")
	{
		HttpRequest->SetVerb(TEXT("DELETE"));
	}

	HttpRequest->OnProcessRequestComplete().BindLambda([Number](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);

		FString ResponseContent = HttpResponse->GetContentAsString();

		int NumberReturned = FCString::Atoi(*ResponseContent);
		CHECK(Number == NumberReturned);

		});
	HttpRequest->ProcessRequest();
}

#endif //UE_HTTP_SUPPORT_UNIX_SOCKET

