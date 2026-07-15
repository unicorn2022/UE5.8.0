// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_LOW_LEVEL_TESTS

#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "DerivedDataCache.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataLegacyCacheStore.h"
#include "DerivedDataRequestOwner.h"
#include "DerivedDataValue.h"
#include "DerivedDataValueId.h"
#include "Experimental/ZenServerInterface.h"
#include "HAL/FileManager.h"
#include "Memory/CompositeBuffer.h"
#include "Misc/Optional.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "Serialization/CompactBinaryWriter.h"

#include "TestHarness.h"

#define UE_HTTPCACHESTORETEST_USE_ZEN PLATFORM_WINDOWS

namespace UE::DerivedData
{

ILegacyCacheStore* GetAnyHttpCacheStore(
	FString& OutDomain,
	FString& OutAccessToken,
	FString& OutNamespace);

class FHttpCacheStoreFixture
{
public:
	FHttpCacheStoreFixture()
		: CloudCache(CreateCache(TEXTVIEW("(Cloud)")))
	{
		if (!CloudCache)
		{
			return;
		}

		// Call GetAnyHttpCacheStore to populate the credentials for Zen upstream configuration,
		// but use CreateCache for the actual test cache.
		GetAnyHttpCacheStore(TestDomain, TestAccessToken, TestNamespace);

#if UE_HTTPCACHESTORETEST_USE_ZEN
		Zen::Private::SetLocalInstallPathOverride(*FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineSavedDir(), "ZenUnitTestInstall")));
		ON_SCOPE_EXIT
		{
			Zen::Private::SetLocalInstallPathOverride(TEXT(""));
		};

		using namespace UE::Zen;
		FServiceSettings ZenUpstreamTestServiceSettings;
		FServiceAutoLaunchSettings& ZenUpstreamTestAutoLaunchSettings = ZenUpstreamTestServiceSettings.SettingsVariant.Get<FServiceAutoLaunchSettings>();
		ZenUpstreamTestAutoLaunchSettings.DataPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineSavedDir(), "ZenUpstreamUnitTest"));
		ZenUpstreamTestAutoLaunchSettings.ExtraArgs =
			FString::Printf(TEXT("--http asio --upstream-jupiter-url \"%s\" --upstream-jupiter-token \"%s\" --upstream-jupiter-namespace \"%s\""),
				*TestDomain, *TestAccessToken, *TestNamespace);
		ZenUpstreamTestAutoLaunchSettings.DesiredPort = 23337; // Avoid the normal default port
		ZenUpstreamTestAutoLaunchSettings.bShowConsole = true;
		ZenUpstreamTestAutoLaunchSettings.bLimitProcessLifetime = true;
		FScopeZenService ScopeZenUpstreamService(MoveTemp(ZenUpstreamTestServiceSettings));

		IFileManager::Get().DeleteDirectory(*FPaths::Combine(FPaths::EngineSavedDir(), "ZenUpstreamSiblingUnitTest"), false, true);
		FServiceSettings ZenUpstreamSiblingTestServiceSettings;
		FServiceAutoLaunchSettings& ZenUpstreamSiblingTestAutoLaunchSettings = ZenUpstreamSiblingTestServiceSettings.SettingsVariant.Get<FServiceAutoLaunchSettings>();
		ZenUpstreamSiblingTestAutoLaunchSettings.DataPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineSavedDir(), "ZenUpstreamSiblingUnitTest"));
		ZenUpstreamSiblingTestAutoLaunchSettings.ExtraArgs =
			FString::Printf(TEXT("--http asio --upstream-jupiter-url \"%s\" --upstream-jupiter-token \"%s\" --upstream-jupiter-namespace \"%s\""),
				*TestDomain, *TestAccessToken, *TestNamespace);
		ZenUpstreamSiblingTestAutoLaunchSettings.DesiredPort = 23338; // Avoid the normal default port
		ZenUpstreamSiblingTestAutoLaunchSettings.bShowConsole = true;
		ZenUpstreamSiblingTestAutoLaunchSettings.bLimitProcessLifetime = true;
		FScopeZenService ScopeZenUpstreamSiblingService(MoveTemp(ZenUpstreamSiblingTestServiceSettings));

		FServiceSettings ZenTestServiceSettings;
		FServiceAutoLaunchSettings& ZenTestAutoLaunchSettings = ZenTestServiceSettings.SettingsVariant.Get<FServiceAutoLaunchSettings>();
		ZenTestAutoLaunchSettings.DataPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineSavedDir(), "ZenUnitTest"));
		ZenTestAutoLaunchSettings.ExtraArgs = FString::Printf(TEXT("--http asio --upstream-zen-url \"http://localhost:%d\""),
			ScopeZenUpstreamService.GetInstance().GetEndpoint().GetPort());
		ZenTestAutoLaunchSettings.DesiredPort = 13337; // Avoid the normal default port
		ZenTestAutoLaunchSettings.bShowConsole = true;
		ZenTestAutoLaunchSettings.bLimitProcessLifetime = true;

		IFileManager::Get().DeleteDirectory(*FPaths::Combine(FPaths::EngineSavedDir(), "ZenUnitTestSibling"), false, true);
		FServiceSettings ZenTestServiceSiblingSettings;
		FServiceAutoLaunchSettings& ZenTestSiblingAutoLaunchSettings = ZenTestServiceSiblingSettings.SettingsVariant.Get<FServiceAutoLaunchSettings>();
		ZenTestSiblingAutoLaunchSettings.DataPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineSavedDir(), "ZenUnitTestSibling"));
		ZenTestSiblingAutoLaunchSettings.ExtraArgs = FString::Printf(TEXT("--http asio --upstream-zen-url \"http://localhost:%d\""),
			ScopeZenUpstreamSiblingService.GetInstance().GetEndpoint().GetPort());
		ZenTestSiblingAutoLaunchSettings.DesiredPort = 13338; // Avoid the normal default port
		ZenTestSiblingAutoLaunchSettings.bShowConsole = true;
		ZenTestSiblingAutoLaunchSettings.bLimitProcessLifetime = true;

		ScopeZenSiblingService.Emplace(MoveTemp(ZenTestServiceSiblingSettings));
		ZenIntermediarySiblingCache.Reset(CreateCache(FString::Printf(TEXT("(TestSibling=(Type=Zen, Host=%s, StructuredNamespace=%s))"),
			*ScopeZenSiblingService->GetInstance().GetEndpoint().GetURL(), *TestNamespace)));

		ScopeZenService.Emplace(MoveTemp(ZenTestServiceSettings));
		ZenIntermediaryCache.Reset(CreateCache(FString::Printf(TEXT("(Test=(Type=Zen, Host=%s, StructuredNamespace=%s))"),
			*ScopeZenService->GetInstance().GetEndpoint().GetURL(), *TestNamespace)));
		CHECK(ZenIntermediaryCache);
	#endif // UE_HTTPCACHESTORETEST_USE_ZEN
	}

protected:
	static void ConcurrentTestWithStats(TFunctionRef<void()> TestFunction, int32 ThreadCount, double Duration)
	{
		std::atomic<uint64> Requests{ 0 };
		std::atomic<uint64> MaxLatency{ 0 };
		std::atomic<uint64> TotalCycles{ 0 };
		std::atomic<uint64> TotalRequests{ 0 };

		FEvent* StartEvent = FPlatformProcess::GetSynchEventFromPool(true);
		FEvent* LastEvent = FPlatformProcess::GetSynchEventFromPool(true);
		std::atomic<double> StopTime{ 0.0 };
		std::atomic<uint64> ActiveCount{ 0 };

		for (int32 ThreadIndex = 0; ThreadIndex < ThreadCount; ++ThreadIndex)
		{
			ActiveCount++;
			Async(
				ThreadIndex < FTaskGraphInterface::Get().GetNumWorkerThreads() ? EAsyncExecution::TaskGraph : EAsyncExecution::Thread,
				[&]()
				{
					// No false start, wait until everyone is ready before starting the test
					StartEvent->Wait();

					while (FPlatformTime::Seconds() < StopTime.load(std::memory_order_relaxed))
					{
						const uint64 Before = FPlatformTime::Cycles64();
						TestFunction();
						const uint64 Delta = FPlatformTime::Cycles64() - Before;
						Requests++;
						TotalCycles += Delta;
						TotalRequests++;

						// Compare exchange loop until we either succeed to set the maximum value
						// or we bail out because we don't have the maximum value anymore.
						while (true)
						{
							uint64 Snapshot = MaxLatency.load();
							if (Delta > Snapshot)
							{
								// Only do the exchange if the value has not changed since we confirmed
								// we had a bigger one.
								if (MaxLatency.compare_exchange_strong(Snapshot, Delta))
								{
									// Exchange succeeded
									break;
								}
							}
							else
							{
								// We don't have the maximum
								break;
							}
						}
					}

					if (--ActiveCount == 0)
					{
						LastEvent->Trigger();
					}
				}
			);
		}

		StopTime = FPlatformTime::Seconds() + Duration;

		// GO!
		StartEvent->Trigger();

		while (FPlatformTime::Seconds() < StopTime)
		{
			FPlatformProcess::Sleep(1.0f);

			if (TotalRequests)
			{
				UE_LOG(LogDerivedDataCache, Display, TEXT("RPS: %" UINT64_FMT ", AvgLatency: %.02f ms, MaxLatency: %.02f s"),
					Requests.exchange(0), FPlatformTime::ToMilliseconds64(TotalCycles) / double(TotalRequests), FPlatformTime::ToSeconds64(MaxLatency));
			}
			else
			{
				UE_LOG(LogDerivedDataCache, Display, TEXT("RPS: %" UINT64_FMT ", AvgLatency: N/A, MaxLatency: %.02f s"),
					Requests.exchange(0), FPlatformTime::ToSeconds64(MaxLatency));
			}
		}

		LastEvent->Wait();

		FPlatformProcess::ReturnSynchEventToPool(StartEvent);
		FPlatformProcess::ReturnSynchEventToPool(LastEvent);
	}

	static bool GetRecords(ICache& Cache, TConstArrayView<FCacheRecord> Records, FCacheRecordPolicy Policy, TArray<FCacheRecord>& OutRecords)
	{
		TArray<FCacheGetRequest> Requests;
		Requests.Reserve(Records.Num());

		for (int32 RecordIndex = 0; RecordIndex < Records.Num(); ++RecordIndex)
		{
			const FCacheRecord& Record = Records[RecordIndex];
			Requests.Add({ {TEXT("HttpCacheStoreTest")}, Record.GetKey(), Policy, static_cast<uint64>(RecordIndex) });
		}

		struct FGetOutput
		{
			FCacheRecord Record;
			EStatus Status = EStatus::Error;
		};

		TArray<TOptional<FGetOutput>> GetOutputs;
		GetOutputs.SetNum(Records.Num());
		FRequestOwner RequestOwner(EPriority::Blocking);
		Cache.Get(Requests, RequestOwner, [&GetOutputs](FCacheGetResponse&& Response)
			{
				FCacheRecordBuilder RecordBuilder(Response.Record.GetKey());

				if (Response.Record.GetMeta())
				{
					RecordBuilder.SetMeta(FCbObject::Clone(Response.Record.GetMeta()));
				}

				for (const FValueWithId& Value : Response.Record.GetValues())
				{
					if (Value)
					{
						RecordBuilder.AddValue(Value);
					}
				}
				GetOutputs[int32(Response.UserData)].Emplace(FGetOutput{ RecordBuilder.Build(), Response.Status });
			});
		RequestOwner.Wait();

		for (int32 RecordIndex = 0; RecordIndex < Records.Num(); ++RecordIndex)
		{
			FGetOutput& ReceivedOutput = GetOutputs[RecordIndex].GetValue();
			if (ReceivedOutput.Status != EStatus::Ok)
			{
				return false;
			}
			OutRecords.Add(MoveTemp(ReceivedOutput.Record));
		}

		return true;
	}

	static bool GetValues(ICache& Cache, TConstArrayView<FValue> Values, ECachePolicy Policy, TArray<FValue>& OutValues, const char* BucketName = nullptr)
	{
		FCacheBucket TestCacheBucket(BucketName ? BucketName : "AutoTestDummy");

		TArray<FCacheGetValueRequest> Requests;
		Requests.Reserve(Values.Num());

		for (int32 ValueIndex = 0; ValueIndex < Values.Num(); ++ValueIndex)
		{
			const FValue& Value = Values[ValueIndex];
			FCacheKey Key;
			Key.Bucket = TestCacheBucket;
			Key.Hash = Value.GetRawHash();
			Requests.Add({ {TEXT("HttpCacheStoreTest")}, Key, Policy, static_cast<uint64>(ValueIndex) });
		}

		struct FGetValueOutput
		{
			FValue Value;
			EStatus Status = EStatus::Error;
		};

		TArray<TOptional<FGetValueOutput>> GetValueOutputs;
		GetValueOutputs.SetNum(Values.Num());
		FRequestOwner RequestOwner(EPriority::Blocking);
		Cache.GetValue(Requests, RequestOwner, [&GetValueOutputs](FCacheGetValueResponse&& Response)
			{
				GetValueOutputs[int32(Response.UserData)].Emplace(FGetValueOutput{ Response.Value, Response.Status });
			});
		RequestOwner.Wait();

		for (int32 ValueIndex = 0; ValueIndex < Values.Num(); ++ValueIndex)
		{
			FGetValueOutput& ReceivedOutput = GetValueOutputs[ValueIndex].GetValue();
			if (ReceivedOutput.Status != EStatus::Ok)
			{
				return false;
			}
			OutValues.Add(MoveTemp(ReceivedOutput.Value));
		}

		return true;
	}

	static bool GetRecordChunks(ICache& Cache, TConstArrayView<FCacheRecord> Records, FCacheRecordPolicy Policy, uint64 Offset, uint64 Size, TArray<FSharedBuffer>& OutChunks)
	{
		TArray<FCacheGetChunkRequest> Requests;

		int32 OverallIndex = 0;
		for (int32 RecordIndex = 0; RecordIndex < Records.Num(); ++RecordIndex)
		{
			const FCacheRecord& Record = Records[RecordIndex];
			TConstArrayView<FValueWithId> Values = Record.GetValues();
			for (int32 ValueIndex = 0; ValueIndex < Values.Num(); ++ValueIndex)
			{
				const FValueWithId& Value = Values[ValueIndex];
				Requests.Add({ {TEXT("HttpCacheStoreTest")}, Record.GetKey(),  Value.GetId(), Offset, Size, Value.GetRawHash(), Policy.GetValuePolicy(Value.GetId()), static_cast<uint64>(OverallIndex) });
				++OverallIndex;
			}
		}

		struct FGetChunksOutput
		{
			FSharedBuffer Chunk;
			EStatus Status = EStatus::Error;
		};

		TArray<TOptional<FGetChunksOutput>> GetOutputs;
		GetOutputs.SetNum(Requests.Num());
		FRequestOwner RequestOwner(EPriority::Blocking);
		Cache.GetChunks(Requests, RequestOwner, [&GetOutputs](FCacheGetChunkResponse&& Response)
			{
				GetOutputs[int32(Response.UserData)].Emplace(FGetChunksOutput { Response.RawData, Response.Status });
			});
		RequestOwner.Wait();

		for (int32 RequestIndex = 0; RequestIndex < Requests.Num(); ++RequestIndex)
		{
			FGetChunksOutput& ReceivedOutput = GetOutputs[RequestIndex].GetValue();
			if (ReceivedOutput.Status != EStatus::Ok)
			{
				return false;
			}
			OutChunks.Add(MoveTemp(ReceivedOutput.Chunk));
		}

		return true;
	}

	static void ValidateRecords(const TCHAR* Name, TConstArrayView<FCacheRecord> RecordsToTest, TConstArrayView<FCacheRecord> ReferenceRecords, FCacheRecordPolicy Policy)
	{
		INFO(Name);
		REQUIRE(RecordsToTest.Num() == ReferenceRecords.Num());

		for (int32 RecordIndex = 0; RecordIndex < RecordsToTest.Num(); ++RecordIndex)
		{
			const FCacheRecord& ExpectedRecord = ReferenceRecords[RecordIndex];
			const FCacheRecord& RecordToTest = RecordsToTest[RecordIndex];

			if (EnumHasAnyFlags(Policy.GetRecordPolicy(), ECachePolicy::SkipMeta))
			{
				CHECK_FALSE(RecordToTest.GetMeta());
			}
			else
			{
				CHECK(ExpectedRecord.GetMeta().Equals(RecordToTest.GetMeta()));
			}

			CHECK(ExpectedRecord.GetValues().Num() == RecordToTest.GetValues().Num());

			const TConstArrayView<FValueWithId> ExpectedValues = ExpectedRecord.GetValues();
			const TConstArrayView<FValueWithId> ReceivedValues = RecordToTest.GetValues();
			for (int32 ValueIndex = 0; ValueIndex < ExpectedValues.Num(); ++ValueIndex)
			{
				INFO(*WriteToAnsiString<64>("Value[", ValueIndex, "]"));
				if (EnumHasAnyFlags(Policy.GetRecordPolicy(), ECachePolicy::SkipData))
				{
					CHECK_FALSE(ReceivedValues[ValueIndex].HasData());
				}
				else
				{
					CHECK(ReceivedValues[ValueIndex].HasData());
					CHECK(ExpectedValues[ValueIndex] == ReceivedValues[ValueIndex]);
					CHECK(FIoHash::HashBuffer(ReceivedValues[ValueIndex].GetData().GetCompressed()) == FIoHash::HashBuffer(ExpectedValues[ValueIndex].GetData().GetCompressed()));
				}
			}
		}
	}

	static void ValidateValues(const TCHAR* Name, TConstArrayView<FValue> ValuesToTest, TConstArrayView<FValue> ReferenceValues, ECachePolicy Policy)
	{
		INFO(Name);
		REQUIRE(ValuesToTest.Num() == ReferenceValues.Num());

		for (int32 ValueIndex = 0; ValueIndex < ValuesToTest.Num(); ++ValueIndex)
		{
			INFO(*WriteToAnsiString<64>("Value[", ValueIndex, "]"));

			const FValue& ExpectedValue = ReferenceValues[ValueIndex];
			const FValue& ValueToTest = ValuesToTest[ValueIndex];

			if (EnumHasAnyFlags(Policy, ECachePolicy::SkipData))
			{
				CHECK_FALSE(ValueToTest.HasData());
			}
			else
			{
				CHECK(ValueToTest.HasData());
				CHECK(ExpectedValue == ValueToTest);
				CHECK(FIoHash::HashBuffer(ValueToTest.GetData().GetCompressed()) == FIoHash::HashBuffer(ExpectedValue.GetData().GetCompressed()));
			}
		}
	}

	static void ValidateRecordChunks(const TCHAR* Name, TConstArrayView<FSharedBuffer> RecordChunksToTest, TConstArrayView<FCacheRecord> ReferenceRecords, FCacheRecordPolicy Policy, uint64 Offset, uint64 Size)
	{
		INFO(Name);

		int32 TotalChunks = 0;
		for (int32 RecordIndex = 0; RecordIndex < ReferenceRecords.Num(); ++RecordIndex)
		{
			const FCacheRecord& Record = ReferenceRecords[RecordIndex];
			TConstArrayView<FValueWithId> Values = Record.GetValues();
			for (int32 ValueIndex = 0; ValueIndex < Values.Num(); ++ValueIndex)
			{
				++TotalChunks;
			}
		}

		REQUIRE(RecordChunksToTest.Num() == TotalChunks);

		int32 ChunkIndex = 0;
		for (int32 RecordIndex = 0; RecordIndex < ReferenceRecords.Num(); ++RecordIndex)
		{
			const FCacheRecord& ExpectedRecord = ReferenceRecords[RecordIndex];

			const TConstArrayView<FValueWithId> ExpectedValues = ExpectedRecord.GetValues();
			for (int32 ValueIndex = 0; ValueIndex < ExpectedValues.Num(); ++ValueIndex)
			{
				INFO(*WriteToAnsiString<64>("Chunk[", ChunkIndex, "]"));

				const FSharedBuffer& ChunkToTest = RecordChunksToTest[ChunkIndex];

				if (EnumHasAnyFlags(Policy.GetRecordPolicy(), ECachePolicy::SkipData))
				{
					CHECK(ChunkToTest.IsNull());
				}
				else
				{
					FSharedBuffer ReferenceBuffer = ExpectedValues[ValueIndex].GetData().Decompress();
					FMemoryView ReferenceView = ReferenceBuffer.GetView().Mid(Offset, Size);
					CHECK(ReferenceView.EqualBytes(ChunkToTest.GetView()));
				}
				++ChunkIndex;
			}
		}
	}

	static TArray<FCacheRecord> GetAndValidateRecords(ICache& Cache, const TCHAR* Name, TConstArrayView<FCacheRecord> Records, FCacheRecordPolicy Policy)
	{
		TArray<FCacheRecord> ReceivedRecords;
		bool bGetSuccessful = GetRecords(Cache, Records, Policy, ReceivedRecords);
		CHECK(bGetSuccessful);

		if (!bGetSuccessful)
		{
			return TArray<FCacheRecord>();
		}

		ValidateRecords(Name, ReceivedRecords, Records, Policy);
		return ReceivedRecords;
	}

	static TArray<FValue> GetAndValidateValues(ICache& Cache, const TCHAR* Name, TConstArrayView<FValue> Values, ECachePolicy Policy)
	{
		TArray<FValue> ReceivedValues;
		bool bGetSuccessful = GetValues(Cache, Values, Policy, ReceivedValues);
		CHECK(bGetSuccessful);

		if (!bGetSuccessful)
		{
			return TArray<FValue>();
		}

		ValidateValues(Name, ReceivedValues, Values, Policy);
		return ReceivedValues;
	}

	static TArray<FSharedBuffer> GetAndValidateRecordChunks(ICache& Cache, const TCHAR* Name, TConstArrayView<FCacheRecord> Records, FCacheRecordPolicy Policy, uint64 Offset, uint64 Size)
	{
		TArray<FSharedBuffer> ReceivedChunks;
		bool bGetSuccessful = GetRecordChunks(Cache, Records, Policy, Offset, Size, ReceivedChunks);
		CHECK(bGetSuccessful);

		if (!bGetSuccessful)
		{
			return TArray<FSharedBuffer>();
		}

		ValidateRecordChunks(Name, ReceivedChunks, Records, Policy, Offset, Size);
		return ReceivedChunks;
	}

	static TArray<FCacheRecord> GetAndValidateRecordsAndChunks(ICache& Cache, const TCHAR* Name, TConstArrayView<FCacheRecord> Records, FCacheRecordPolicy Policy)
	{
		GetAndValidateRecordChunks(Cache, Name, Records, Policy, 5, 5);
		return GetAndValidateRecords(Cache, Name, Records, Policy);
	}

	static TArray<FCacheRecord> CreateTestCacheRecords(ICache& Cache, uint32 InNumKeys, uint32 InNumValues, FCbObject MetaContents = FCbObject(), const char* BucketName = nullptr, uint8 Salt = 0)
	{
		FCacheBucket TestCacheBucket(BucketName ? BucketName : "AutoTestDummy");

		TArray<FCacheRecord> CacheRecords;
		TArray<FCachePutRequest> PutRequests;
		PutRequests.Reserve(InNumKeys);
		const uint32 KeySalt = 1;

		for (uint32 KeyIndex = 0; KeyIndex < InNumKeys; ++KeyIndex)
		{
			FIoHashBuilder HashBuilder;
			HashBuilder.Update(&KeySalt,sizeof(KeySalt));

			TArray<FSharedBuffer> Values;
			for (uint32 ValueIndex = 0; ValueIndex < InNumValues; ++ValueIndex)
			{
				TArray<uint8> ValueContents;
				// Add N zeroed bytes where N corresponds to the value index times 10.
				const int32 NumBytes = (ValueIndex+1)*10;
				ValueContents.AddUninitialized(NumBytes);
				for (int32 ContentIndex = 0; ContentIndex < NumBytes; ++ContentIndex)
				{
					ValueContents[ContentIndex] = (uint8)(KeyIndex + ContentIndex + Salt);
				}
				Values.Emplace(MakeSharedBufferFromArray(MoveTemp(ValueContents)));
				HashBuilder.Update(Values.Last().GetView());
			}

			if (MetaContents)
			{
				MetaContents.AppendHash(HashBuilder);
			}

			FCacheKey Key;
			Key.Bucket = TestCacheBucket;
			Key.Hash = HashBuilder.Finalize();

			FCacheRecordBuilder RecordBuilder(Key);

			for (const FSharedBuffer& ValueBuffer : Values)
			{
				FIoHash ValueHash(FIoHash::HashBuffer(ValueBuffer));
				RecordBuilder.AddValue(FValueId::FromHash(ValueHash), ValueBuffer);
			}

			if (MetaContents)
			{
				RecordBuilder.SetMeta(MoveTemp(MetaContents));
			}

			PutRequests.Add({ {TEXT("AutoTest")}, RecordBuilder.Build(), ECachePolicy::Default, KeyIndex });
		}

		FRequestOwner Owner(EPriority::Blocking);
		Cache.Put(PutRequests, Owner, [&CacheRecords, &PutRequests] (FCachePutResponse&& Response)
		{
			check(Response.Status == EStatus::Ok);
		});
		Owner.Wait();

		CacheRecords.Reserve(PutRequests.Num());
		for (const FCachePutRequest& PutRequest : PutRequests)
		{
			CacheRecords.Add(PutRequest.Record);
		}

		CHECK(CacheRecords.Num() == InNumKeys);
		return CacheRecords;
	}

	static TArray<FValue> CreateTestCacheValues(ICache& Cache, uint32 InNumValues, const char* BucketName = nullptr, uint8 Salt = 1)
	{
		FCacheBucket TestCacheBucket(BucketName ? BucketName : "AutoTestDummy");

		TArray<FCachePutValueRequest> PutValueRequests;
		PutValueRequests.Reserve(InNumValues);

		TArray<FSharedBuffer> ValueBuffers;
		for (uint32 ValueIndex = 0; ValueIndex < InNumValues; ++ValueIndex)
		{
			TArray<uint8> ValueContents;
			// Add N zeroed bytes where N corresponds to the value index times 10.
			const int32 NumBytes = (ValueIndex+1)*10;
			ValueContents.AddUninitialized(NumBytes);
			for (int32 ContentIndex = 0; ContentIndex < NumBytes; ++ContentIndex)
			{
				ValueContents[ContentIndex] = (uint8)(ValueIndex + ContentIndex + Salt);
			}
			ValueBuffers.Emplace(MakeSharedBufferFromArray(MoveTemp(ValueContents)));
		}

		uint64 KeyIndex = 0;
		for (const FSharedBuffer& ValueBuffer : ValueBuffers)
		{
			FIoHash ValueHash(FIoHash::HashBuffer(ValueBuffer));

			FCacheKey Key;
			Key.Bucket = TestCacheBucket;
			Key.Hash = ValueHash;

			PutValueRequests.Add({ {TEXT("AutoTest")}, Key, FValue::Compress(ValueBuffer), ECachePolicy::Default, KeyIndex++ });
		}

		TArray<FValue> Values;
		FRequestOwner Owner(EPriority::Blocking);
		Cache.PutValue(PutValueRequests, Owner, [&Values, &PutValueRequests](FCachePutValueResponse&& Response)
			{
				check(Response.Status == EStatus::Ok);
			});
		Owner.Wait();

		Values.Reserve(PutValueRequests.Num());
		for (const FCachePutValueRequest& PutValueRequest : PutValueRequests)
		{
			Values.Add(PutValueRequest.Value);
		}

		CHECK(Values.Num() == InNumValues);
		return Values;
	}

#if UE_HTTPCACHESTORETEST_USE_ZEN
	void WaitForZenPushToUpstream(ICache* ZenBackend, TConstArrayView<FCacheRecord> Records) const
	{
		// TODO: Expecting a legitimate means to wait for zen to finish pushing records to its upstream in the future
		FPlatformProcess::Sleep(1.0f);
	}

	void WaitForZenPushValuesToUpstream(ICache* ZenBackend, TConstArrayView<FValue> Values) const
	{
		// TODO: Expecting a legitimate means to wait for zen to finish pushing records to its upstream in the future
		FPlatformProcess::Sleep(1.0f);
	}
#endif // UE_HTTPCACHESTORETEST_USE_ZEN

	TUniquePtr<ICache> CloudCache;
	FString TestDomain;
	FString TestAccessToken;
	FString TestNamespace;

#if UE_HTTPCACHESTORETEST_USE_ZEN
	TOptional<Zen::FScopeZenService> ScopeZenSiblingService;
	TUniquePtr<ICache> ZenIntermediarySiblingCache;
	TOptional<Zen::FScopeZenService> ScopeZenService;
	TUniquePtr<ICache> ZenIntermediaryCache;
#endif // UE_HTTPCACHESTORETEST_USE_ZEN
};

TEST_CASE_PERSISTENT_FIXTURE(FHttpCacheStoreFixture, "DerivedData::CacheStore::Cloud", "[DerivedData]")
{
	if (!CloudCache)
	{
		SKIP("Cloud cache store could not be created.");
	}

	const uint32 RecordsInBatch = 3;
	const uint32 ValuesInBatch = RecordsInBatch;

	SECTION("SimpleValue")
	{
		TArray<FCacheRecord> PutRecords = CreateTestCacheRecords(*CloudCache, RecordsInBatch, 1);
		TArray<FCacheRecord> RecievedRecords = GetAndValidateRecordsAndChunks(*CloudCache, TEXT("SimpleValue"), PutRecords, ECachePolicy::Default);
		TArray<FCacheRecord> RecievedRecordsSkipMeta = GetAndValidateRecordsAndChunks(*CloudCache, TEXT("SimpleValueSkipMeta"), PutRecords, ECachePolicy::Default | ECachePolicy::SkipMeta);
		TArray<FCacheRecord> RecievedRecordsSkipData = GetAndValidateRecordsAndChunks(*CloudCache, TEXT("SimpleValueSkipData"), PutRecords, ECachePolicy::Default | ECachePolicy::SkipData);

#if UE_HTTPCACHESTORETEST_USE_ZEN
		if (ZenIntermediaryCache)
		{
			TArray<FCacheRecord> PutRecordsZen = CreateTestCacheRecords(*ZenIntermediaryCache, RecordsInBatch, 1, FCbObject(), "AutoTestDummyZen");
			WaitForZenPushToUpstream(ZenIntermediaryCache.Get(), PutRecordsZen);
			ValidateRecords(TEXT("SimpleValueZenAndDirect"), GetAndValidateRecords(*ZenIntermediaryCache, TEXT("SimpleValueZen"), PutRecordsZen, ECachePolicy::Default), RecievedRecords, ECachePolicy::Default);
			ValidateRecords(TEXT("SimpleValueSkipMetaZenAndDirect"), GetAndValidateRecords(*ZenIntermediaryCache, TEXT("SimpleValueSkipMetaZen"), PutRecordsZen, ECachePolicy::Default | ECachePolicy::SkipMeta), RecievedRecordsSkipMeta, ECachePolicy::Default | ECachePolicy::SkipMeta);
			ValidateRecords(TEXT("SimpleValueSkipDataZenAndDirect"), GetAndValidateRecords(*ZenIntermediaryCache, TEXT("SimpleValueSkipDataZen"), PutRecordsZen, ECachePolicy::Default | ECachePolicy::SkipData), RecievedRecordsSkipData, ECachePolicy::Default | ECachePolicy::SkipData);
		}
#endif // UE_HTTPCACHESTORETEST_USE_ZEN
	}

	SECTION("SimpleValueWithMeta")
	{
		TCbWriter<64> MetaWriter;
		MetaWriter.BeginObject();
		MetaWriter.AddInteger(ANSITEXTVIEW("MetaKey"), 42);
		MetaWriter.EndObject();
		FCbObject MetaObject = MetaWriter.Save().AsObject();

		TArray<FCacheRecord> PutRecords = CreateTestCacheRecords(*CloudCache, RecordsInBatch, 1, MetaObject);
		TArray<FCacheRecord> RecievedRecords = GetAndValidateRecordsAndChunks(*CloudCache, TEXT("SimpleValueWithMeta"), PutRecords, ECachePolicy::Default);
		TArray<FCacheRecord> RecievedRecordsSkipMeta = GetAndValidateRecordsAndChunks(*CloudCache, TEXT("SimpleValueWithMetaSkipMeta"), PutRecords, ECachePolicy::Default | ECachePolicy::SkipMeta);
		TArray<FCacheRecord> RecievedRecordsSkipData = GetAndValidateRecordsAndChunks(*CloudCache, TEXT("SimpleValueWithMetaSkipData"), PutRecords, ECachePolicy::Default | ECachePolicy::SkipData);

#if UE_HTTPCACHESTORETEST_USE_ZEN
		if (ZenIntermediaryCache)
		{
			TArray<FCacheRecord> PutRecordsZen = CreateTestCacheRecords(*ZenIntermediaryCache, RecordsInBatch, 1, MetaObject, "AutoTestDummyZen");
			WaitForZenPushToUpstream(ZenIntermediaryCache.Get(), PutRecordsZen);
			ValidateRecords(TEXT("SimpleValueWithMetaZenAndDirect"), GetAndValidateRecords(*ZenIntermediaryCache, TEXT("SimpleValueWithMetaZen"), PutRecordsZen, ECachePolicy::Default), RecievedRecords, ECachePolicy::Default);
			ValidateRecords(TEXT("SimpleValueWithMetaSkipMetaZenAndDirect"), GetAndValidateRecords(*ZenIntermediaryCache, TEXT("SimpleValueWithMetaSkipMetaZen"), PutRecordsZen, ECachePolicy::Default | ECachePolicy::SkipMeta), RecievedRecordsSkipMeta, ECachePolicy::Default | ECachePolicy::SkipMeta);
			ValidateRecords(TEXT("SimpleValueWithMetaSkipDataZenAndDirect"), GetAndValidateRecords(*ZenIntermediaryCache, TEXT("SimpleValueWithMetaSkipDataZen"), PutRecordsZen, ECachePolicy::Default | ECachePolicy::SkipData), RecievedRecordsSkipData, ECachePolicy::Default | ECachePolicy::SkipData);
		}
#endif // UE_HTTPCACHESTORETEST_USE_ZEN
	}

	SECTION("MultiValue")
	{
		TArray<FCacheRecord> PutRecords = CreateTestCacheRecords(*CloudCache, RecordsInBatch, 5);
		TArray<FCacheRecord> RecievedRecords = GetAndValidateRecordsAndChunks(*CloudCache, TEXT("MultiValue"), PutRecords, ECachePolicy::Default);
		TArray<FCacheRecord> RecievedRecordsSkipMeta = GetAndValidateRecordsAndChunks(*CloudCache, TEXT("MultiValueSkipMeta"), PutRecords, ECachePolicy::Default | ECachePolicy::SkipMeta);
		TArray<FCacheRecord> RecievedRecordsSkipData = GetAndValidateRecordsAndChunks(*CloudCache, TEXT("MultiValueSkipData"), PutRecords, ECachePolicy::Default | ECachePolicy::SkipData);

#if UE_HTTPCACHESTORETEST_USE_ZEN
		if (ZenIntermediaryCache)
		{
			TArray<FCacheRecord> PutRecordsZen = CreateTestCacheRecords(*ZenIntermediaryCache, RecordsInBatch, 5, FCbObject(), "AutoTestDummyZen");
			WaitForZenPushToUpstream(ZenIntermediaryCache.Get(), PutRecordsZen);
			ValidateRecords(TEXT("MultiValueZenAndDirect"), GetAndValidateRecords(*ZenIntermediaryCache, TEXT("MultiValueZen"), PutRecordsZen, ECachePolicy::Default), RecievedRecords, ECachePolicy::Default);
			ValidateRecords(TEXT("MultiValueSkipMetaZenAndDirect"), GetAndValidateRecords(*ZenIntermediaryCache, TEXT("MultiValueSkipMetaZen"), PutRecordsZen, ECachePolicy::Default | ECachePolicy::SkipMeta), RecievedRecordsSkipMeta, ECachePolicy::Default | ECachePolicy::SkipMeta);
			ValidateRecords(TEXT("MultiValueSkipDataZenAndDirect"), GetAndValidateRecords(*ZenIntermediaryCache, TEXT("MultiValueSkipDataZen"), PutRecordsZen, ECachePolicy::Default | ECachePolicy::SkipData), RecievedRecordsSkipData, ECachePolicy::Default | ECachePolicy::SkipData);
		}
#endif // UE_HTTPCACHESTORETEST_USE_ZEN
	}

	SECTION("SimpleValues")
	{
		TArray<FValue> PutValues = CreateTestCacheValues(*CloudCache, ValuesInBatch);
		TArray<FValue> ReceivedValues = GetAndValidateValues(*CloudCache, TEXT("SimpleValue"), PutValues, ECachePolicy::Default);
		TArray<FValue> ReceivedValuesSkipData = GetAndValidateValues(*CloudCache, TEXT("SimpleValueSkipData"), PutValues, ECachePolicy::Default | ECachePolicy::SkipData);

#if UE_HTTPCACHESTORETEST_USE_ZEN
		if (ZenIntermediaryCache)
		{
			TArray<FValue> PutValuesZen = CreateTestCacheValues(*ZenIntermediaryCache, ValuesInBatch);
			WaitForZenPushValuesToUpstream(ZenIntermediaryCache.Get(), PutValuesZen);
			ValidateValues(TEXT("SimpleValueZenAndDirect"), GetAndValidateValues(*ZenIntermediaryCache, TEXT("SimpleValueZen"), PutValuesZen, ECachePolicy::Default), ReceivedValues, ECachePolicy::Default);
			ValidateValues(TEXT("SimpleValueSkipDataZenAndDirect"), GetAndValidateValues(*ZenIntermediaryCache, TEXT("SimpleValueSkipDataZen"), PutValuesZen, ECachePolicy::Default | ECachePolicy::SkipData), ReceivedValuesSkipData, ECachePolicy::Default | ECachePolicy::SkipData);
		}
		// Disabled because we have no guarantee that the upstream server received the put in any particular timeframe.
		//CHECKED_IF(ZenIntermediarySiblingCache)
		//{
		//	GetAndValidateValues(*ZenIntermediarySiblingCache, TEXT("SimpleValueZen"), PutValues, ECachePolicy::Default);
		//	GetAndValidateValues(*ZenIntermediarySiblingCache, TEXT("SimpleValueSkipDataZen"), PutValues, ECachePolicy::Default | ECachePolicy::SkipData);
		//}
#endif // UE_HTTPCACHESTORETEST_USE_ZEN
	}
}

static void PutTestRecords(ICache& Cache, uint32 NumRecords, bool bSynchronous = false, TArray<FCacheKey>* OutKeys = nullptr)
{
	FCacheBucket TestCacheBucket("AutoTestDummy");

	const int64 RunUniqueId = FDateTime::Now().GetTicks();

	TArray<FCachePutRequest> PutRequests;
	PutRequests.Reserve(NumRecords);

	TArray<FSharedBuffer> ValueBuffers;
	for (uint32 ValueIndex = 0; ValueIndex < NumRecords; ++ValueIndex)
	{
		TArray<uint8> ValueContents;
		const int32 NumBytes = 12;
		ValueContents.AddUninitialized(NumBytes);
		*reinterpret_cast<int64*>(ValueContents.GetData()) = RunUniqueId;
		*(reinterpret_cast<uint32*>(ValueContents.GetData()) + 2) = ValueIndex;
		ValueBuffers.Emplace(MakeSharedBufferFromArray(MoveTemp(ValueContents)));
	}

	if (OutKeys)
	{
		OutKeys->Reserve(OutKeys->Num() + NumRecords);
	}
	uint64 KeyIndex = 0;
	for (const FSharedBuffer& ValueBuffer : ValueBuffers)
	{
		FIoHash ValueHash(FIoHash::HashBuffer(ValueBuffer));

		FCacheKey Key;
		Key.Bucket = TestCacheBucket;
		Key.Hash = ValueHash;

		if (OutKeys)
		{
			OutKeys->Add(Key);
		}

		FCacheRecordBuilder RecordBuilder(Key);
		RecordBuilder.AddValue(FValueId::FromHash(ValueHash), ValueBuffer);

		PutRequests.Add({ {TEXT("AutoTestStressPutRecord")}, RecordBuilder.Build(), ECachePolicy::Default, KeyIndex });
	}

	FRequestOwner RequestOwner(bSynchronous ? EPriority::Blocking : EPriority::Normal);
	{
		FRequestBarrier RequestBarrier(RequestOwner);
		if (!bSynchronous)
		{
			RequestOwner.KeepAlive();
		}
		Cache.Put(PutRequests, RequestOwner, [](FCachePutResponse&& Response)
		{
			check(Response.Status == EStatus::Ok);
		});
	}

	if (bSynchronous)
	{
		RequestOwner.Wait();
	}
}

TEST_CASE("DerivedData::CacheStore::Cloud::StressPut", "[.][DerivedData]")
{
	TUniquePtr<ICache> CloudCache(CreateCache(TEXTVIEW("(Cloud)")));
	if (!CloudCache)
	{
		SKIP("Cloud cache store could not be created.");
	}

	constexpr uint32 NumRecords = 1000;
	{
		FAutoScopedDurationTimer AutoTimer;
		PutTestRecords(*CloudCache, NumRecords, true);
		UE_LOGF(LogDerivedDataCache, Display, "Putting %u records (containing values of size 12 bytes): %.2f s", NumRecords, AutoTimer.GetTime());
	}
}

} // UE::DerivedData

#endif // WITH_LOW_LEVEL_TESTS
