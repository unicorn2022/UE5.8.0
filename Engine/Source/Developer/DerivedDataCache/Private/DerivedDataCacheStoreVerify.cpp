// Copyright Epic Games, Inc. All Rights Reserved.

#include "Algo/Compare.h"
#include "Algo/Find.h"
#include "Async/UniqueLock.h"
#include "Containers/Set.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataCacheKeyFilter.h"
#include "DerivedDataCacheUsageStats.h"
#include "DerivedDataLegacyCacheStore.h"
#include "HAL/CriticalSection.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/FileHelper.h"
#include "String/Find.h"
#include "Templates/Tuple.h"
#include "Templates/UniquePtr.h"

namespace UE::DerivedData
{

/**
 * A cache store that verifies that derived data is generated deterministically.
 *
 * This wraps a cache store and fails every get until a matching put occurs, then compares the derived data.
 */
class FCacheStoreVerify final : public ILegacyCacheStore
{
public:
	FCacheStoreVerify(ILegacyCacheStore* InInnerCache, bool bInPutOnError)
		: InnerCache(InInnerCache)
		, bPutOnError(bInPutOnError)
	{
		check(InnerCache);

		const TCHAR* const CommandLine = FCommandLine::Get();

		const bool bDefaultMatch = FParse::Param(CommandLine, TEXT("DDC-Verify")) ||
			(String::FindFirst(CommandLine, TEXT("-DDC-Verify="), ESearchCase::IgnoreCase) == INDEX_NONE &&
			 String::FindFirst(CommandLine, TEXT("-DDC-VerifyKeys="), ESearchCase::IgnoreCase) == INDEX_NONE);
		float DefaultRate = bDefaultMatch ? 100.0f : 0.0f;
		FParse::Value(CommandLine, TEXT("-DDC-VerifyRate="), DefaultRate);

		KeyFilter = FCacheKeyFilter::Parse(CommandLine, TEXT("-DDC-Verify="), TEXT("-DDC-VerifyKeys="), DefaultRate);

		uint32 Salt;
		if (FParse::Value(CommandLine, TEXT("-DDC-VerifySalt="), Salt))
		{
			if (Salt == 0)
			{
				UE_LOGF(LogDerivedDataCache, Warning,
					"Verify: Ignoring salt of 0. The salt must be a positive integer.");
			}
			else
			{
				KeyFilter.SetSalt(Salt);
			}
		}

		UE_CLOGF(KeyFilter.RequiresSalt(), LogDerivedDataCache, Display,
			"Verify: Using salt -DDC-VerifySalt=%u to filter cache keys to verify.", KeyFilter.GetSalt());

		bPutOnError = bPutOnError || FParse::Param(CommandLine, TEXT("DDC-VerifyFix"));
		UE_CLOGF(bPutOnError, LogDerivedDataCache, Display,
			"Verify: Any record or value that differs will be overwritten.");
	}

	~FCacheStoreVerify()
	{
		delete InnerCache;
	}

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

	void LegacyStats(FDerivedDataCacheStatsNode& OutNode) final
	{
		InnerCache->LegacyStats(OutNode);
	}

	void GatherVerificationStats(FDerivedDataCacheVerificationStats& OutStats) const final;

private:
	struct FVerifyPutState
	{
		TArray<FCachePutRequest> ForwardRequests;
		TArray<FCachePutRequest> VerifyRequests;
		FOnCachePutComplete OnComplete;
		int32 ActiveRequests = 0;
		FRWLock Lock;
	};

	struct FVerifyPutValueState
	{
		TArray<FCachePutValueRequest> ForwardRequests;
		TArray<FCachePutValueRequest> VerifyRequests;
		FOnCachePutValueComplete OnComplete;
		int32 ActiveRequests = 0;
		FRWLock Lock;
	};

	void GetMetaComplete(IRequestOwner& Owner, FVerifyPutState* State, FCacheGetResponse&& Response);
	void GetDataComplete(IRequestOwner& Owner, FVerifyPutState* State, FCacheGetResponse&& Response);
	void GetComplete(IRequestOwner& Owner, FVerifyPutState* State);

	void GetMetaComplete(IRequestOwner& Owner, FVerifyPutValueState* State, FCacheGetValueResponse&& Response);
	void GetDataComplete(IRequestOwner& Owner, FVerifyPutValueState* State, FCacheGetValueResponse&& Response);
	void GetComplete(IRequestOwner& Owner, FVerifyPutValueState* State);

	bool CompareRecords(
		const FCacheRecord& PutRecord,
		const FCacheRecord& GetRecord,
		const FCacheRecordPolicy& Policy,
		const FSharedString& Name);

	static void LogChangedValue(
		const FSharedString& Name,
		const FCacheKey& Key,
		const FValueId& Id,
		const FIoHash& NewRawHash,
		const FIoHash& OldRawHash,
		const FCompositeBuffer& NewRawData,
		const FCompositeBuffer& OldRawData);

	// Track per-bucket verification results
	struct FTrackedEntryStats
	{
		/*
			ModifyT = void(FDerivedDataCacheVerificationStat&), used to write to a bucket and maintain the write lock
		*/
		template <typename ModifyT>
		void Modify(FCacheBucket Bucket, ModifyT DoModification)
		{
			TUniqueLock DoLock(Lock);

			// Bucket names are resolved in GatherVerificationStats
			FDerivedDataCacheVerificationStat& StatsRef = PerBucketStats.FindOrAdd(Bucket);		
			DoModification(StatsRef);
		}

		TMap<FCacheBucket, FDerivedDataCacheVerificationStat> PerBucketStats;
		mutable FMutex Lock;
	};

private:
	FTrackedEntryStats BucketStats;
	ILegacyCacheStore* InnerCache;
	FCriticalSection AlreadyTestedLock;
	TSet<FCacheKey> AlreadyTested;
	FCacheKeyFilter KeyFilter;
	bool bPutOnError;
};

// A helper to get the total size of an array of values in a record
uint64 GetValuesTotalSize(const TConstArrayView<FValueWithId>& Values)
{
	uint64 TotalSize = 0;
	for (const FValueWithId& Value : Values)
	{
		TotalSize += Value.GetRawSize();
	}
	return TotalSize;
}

void FCacheStoreVerify::Put(
	const TConstArrayView<FCachePutRequest> Requests,
	IRequestOwner& Owner,
	FOnCachePutComplete&& OnComplete)
{
	TUniquePtr<FVerifyPutState> State = MakeUnique<FVerifyPutState>();
	State->VerifyRequests.Reserve(Requests.Num());
	{
		FScopeLock Lock(&AlreadyTestedLock);
		for (const FCachePutRequest& Request : Requests)
		{
			const FCacheKey& Key = Request.Record.GetKey();
			bool bForward = EnumHasAnyFlags(Request.Policy.GetRecordPolicy(), ECachePolicy::NonDeterministic) || !KeyFilter.IsMatch(Key);
			if (!bForward)
			{
				AlreadyTested.Add(Key, &bForward);
			}
			(bForward ? State->ForwardRequests : State->VerifyRequests).Add(Request);
			if (!bForward)
			{
				BucketStats.Modify(Request.Record.GetKey().Bucket, [&Request](FDerivedDataCacheVerificationStat& BucketStat) 
				{
					BucketStat.RecordsTested++;
					BucketStat.ValuesTested += Request.Record.GetValues().Num();
					BucketStat.ValuesTestedBytes += GetValuesTotalSize(Request.Record.GetValues());
				});
			}
		}
	}

	if (State->VerifyRequests.IsEmpty())
	{
		return InnerCache->Put(State->ForwardRequests, Owner, MoveTemp(OnComplete));
	}

	TArray<FCacheGetRequest> GetMetaRequests;
	GetMetaRequests.Reserve(State->VerifyRequests.Num());
	{
		uint64 PutIndex = 0;
		const ECachePolicy GetPolicy = ECachePolicy::Query | ECachePolicy::PartialRecord | ECachePolicy::SkipData;
		for (const FCachePutRequest& PutRequest : State->VerifyRequests)
		{
			GetMetaRequests.Add({PutRequest.Name, PutRequest.Record.GetKey(), GetPolicy, PutIndex++});
		}
	}

	State->OnComplete = MoveTemp(OnComplete);
	State->ActiveRequests = GetMetaRequests.Num();
	InnerCache->Get(GetMetaRequests, Owner, [this, &Owner, State = State.Release()](FCacheGetResponse&& MetaResponse)
	{
		GetMetaComplete(Owner, State, MoveTemp(MetaResponse));
	});
}

void FCacheStoreVerify::GetMetaComplete(IRequestOwner& Owner, FVerifyPutState* State, FCacheGetResponse&& Response)
{
	FCachePutRequest& Request = State->VerifyRequests[int32(Response.UserData)];

	if ((Response.Status == EStatus::Ok) ||
		(Response.Status == EStatus::Error && !Response.Record.GetValues().IsEmpty()))
	{
		const auto MakeValueTuple = [](const FValueWithId& Value) -> TTuple<FValueId, FIoHash>
		{
			return MakeTuple(Value.GetId(), Value.GetRawHash());
		};
		if (Algo::CompareBy(Request.Record.GetValues(), Response.Record.GetValues(), MakeValueTuple))
		{
			UE_LOGF(LogDerivedDataCache, Verbose,
				"Verify: Data in the cache matches newly generated data for %ls from '%ls'.",
				*WriteToString<96>(Request.Record.GetKey()), *Request.Name);
			State->OnComplete(Request.MakeResponse(EStatus::Ok));
			BucketStats.Modify(Response.Record.GetKey().Bucket, [&Request](FDerivedDataCacheVerificationStat& BucketStat) 
			{
				BucketStat.RecordsMatchedCache++;
				BucketStat.ValuesMatchedCache += Request.Record.GetValues().Num();
				BucketStat.ValuesMatchedCacheBytes += GetValuesTotalSize(Request.Record.GetValues());
			});
		}
		else
		{
			const ECachePolicy Policy = ECachePolicy::Default | ECachePolicy::PartialRecord;
			const FCacheGetRequest GetDataRequests[]{{Response.Name, Response.Record.GetKey(), Policy, Response.UserData}};
			return InnerCache->Get(GetDataRequests, Owner, [this, &Owner, State](FCacheGetResponse&& DataResponse)
			{
				GetDataComplete(Owner, State, MoveTemp(DataResponse));
			});
		}
	}
	else
	{
		UE_LOGF(LogDerivedDataCache, Warning,
			"Verify: Cache did not contain a record for %ls from '%ls'.",
			*WriteToString<96>(Request.Record.GetKey()), *Request.Name);
		BucketStats.Modify(Response.Record.GetKey().Bucket, [&Request](FDerivedDataCacheVerificationStat& BucketStat) 
		{
			BucketStat.RecordsNotInCache++;
			BucketStat.ValuesNotInCache += Request.Record.GetValues().Num();
			BucketStat.ValuesNotInCacheBytes += GetValuesTotalSize(Request.Record.GetValues());
		});
		FWriteScopeLock Lock(State->Lock);
		State->ForwardRequests.Add(MoveTemp(Request));
	}

	GetComplete(Owner, State);
}

void FCacheStoreVerify::GetDataComplete(IRequestOwner& Owner, FVerifyPutState* State, FCacheGetResponse&& Response)
{
	FCachePutRequest& Request = State->VerifyRequests[int32(Response.UserData)];

	if ((Response.Status == EStatus::Ok) ||
		(Response.Status == EStatus::Error && !Response.Record.GetValues().IsEmpty()))
	{
		if (CompareRecords(Request.Record, Response.Record, Request.Policy, Request.Name))
		{
			UE_LOGF(LogDerivedDataCache, Verbose,
				"Verify: Data in the cache matches newly generated data for %ls from '%ls'.",
				*WriteToString<96>(Request.Record.GetKey()), *Request.Name);
			State->OnComplete(Request.MakeResponse(EStatus::Ok));
		}
		else if (bPutOnError)
		{
			// Ask to overwrite existing records to potentially eliminate the mismatch.
			UE_LOGF(LogDerivedDataCache, Display,
				"Verify: Writing newly generated data to the cache for %ls from '%ls'.",
				*WriteToString<96>(Request.Record.GetKey()), *Request.Name);
			Request.Policy = Request.Policy.Transform([](ECachePolicy P) { return P & ~ECachePolicy::Query; });
			FWriteScopeLock Lock(State->Lock);
			State->ForwardRequests.Add(MoveTemp(Request));
		}
		else
		{
			State->OnComplete(Request.MakeResponse(EStatus::Ok));
		}
	}
	else
	{
		UE_LOGF(LogDerivedDataCache, Warning,
			"Verify: Cache did not contain a record for %ls from '%ls'.",
			*WriteToString<96>(Request.Record.GetKey()), *Request.Name);
		BucketStats.Modify(Request.Record.GetKey().Bucket, [&Request](FDerivedDataCacheVerificationStat& BucketStat) 
		{
			BucketStat.RecordsNotInCache++;
			BucketStat.ValuesNotInCache += Request.Record.GetValues().Num();
			BucketStat.ValuesNotInCacheBytes += GetValuesTotalSize(Request.Record.GetValues());
		});
		FWriteScopeLock Lock(State->Lock);
		State->ForwardRequests.Add(MoveTemp(Request));
	}

	GetComplete(Owner, State);
}

void FCacheStoreVerify::GetComplete(IRequestOwner& Owner, FVerifyPutState* State)
{
	if (FWriteScopeLock Lock(State->Lock); --State->ActiveRequests > 0)
	{
		return;
	}
	if (!State->ForwardRequests.IsEmpty())
	{
		InnerCache->Put(State->ForwardRequests, Owner, MoveTemp(State->OnComplete));
	}
	delete State;
}

void FCacheStoreVerify::Get(
	const TConstArrayView<FCacheGetRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetComplete&& OnComplete)
{
	TArray<FCacheGetRequest, TInlineAllocator<8>> ForwardRequests;
	TArray<FCacheGetRequest, TInlineAllocator<8>> VerifyRequests;
	ForwardRequests.Reserve(Requests.Num());
	VerifyRequests.Reserve(Requests.Num());
	{
		FScopeLock Lock(&AlreadyTestedLock);
		for (const FCacheGetRequest& Request : Requests)
		{
			const bool bForward = EnumHasAnyFlags(Request.Policy.GetRecordPolicy(), ECachePolicy::NonDeterministic) ||
				!KeyFilter.IsMatch(Request.Key) || AlreadyTested.Contains(Request.Key);
			(bForward ? ForwardRequests : VerifyRequests).Add(Request);
		}
	}

	CompleteWithStatus(VerifyRequests, OnComplete, EStatus::Error);

	if (!ForwardRequests.IsEmpty())
	{
		InnerCache->Get(ForwardRequests, Owner, MoveTemp(OnComplete));
	}
}

void FCacheStoreVerify::PutValue(
	const TConstArrayView<FCachePutValueRequest> Requests,
	IRequestOwner& Owner,
	FOnCachePutValueComplete&& OnComplete)
{
	TUniquePtr<FVerifyPutValueState> State = MakeUnique<FVerifyPutValueState>();
	State->VerifyRequests.Reserve(Requests.Num());
	{
		FScopeLock Lock(&AlreadyTestedLock);
		for (const FCachePutValueRequest& Request : Requests)
		{
			bool bForward = EnumHasAnyFlags(Request.Policy, ECachePolicy::NonDeterministic) || !KeyFilter.IsMatch(Request.Key);
			if (!bForward)
			{
				AlreadyTested.Add(Request.Key, &bForward);
			}
			(bForward ? State->ForwardRequests : State->VerifyRequests).Add(Request);
			if (!bForward)
			{
				BucketStats.Modify(Request.Key.Bucket, [&Request](FDerivedDataCacheVerificationStat& BucketStat) 
				{
					BucketStat.ValuesTested++;
					BucketStat.ValuesTestedBytes += Request.Value.GetRawSize();
				});
			}
		}
	}

	if (State->VerifyRequests.IsEmpty())
	{
		return InnerCache->PutValue(State->ForwardRequests, Owner, MoveTemp(OnComplete));
	}

	TArray<FCacheGetValueRequest> GetMetaRequests;
	GetMetaRequests.Reserve(State->VerifyRequests.Num());
	{
		uint64 PutIndex = 0;
		const ECachePolicy GetPolicy = ECachePolicy::Query | ECachePolicy::SkipData;
		for (const FCachePutValueRequest& PutRequest : State->VerifyRequests)
		{
			GetMetaRequests.Add({PutRequest.Name, PutRequest.Key, GetPolicy, PutIndex++});
		}
	}

	State->OnComplete = MoveTemp(OnComplete);
	State->ActiveRequests = GetMetaRequests.Num();
	InnerCache->GetValue(GetMetaRequests, Owner, [this, &Owner, State = State.Release()](FCacheGetValueResponse&& MetaResponse)
	{
		GetMetaComplete(Owner, State, MoveTemp(MetaResponse));
	});
}

void FCacheStoreVerify::GetMetaComplete(IRequestOwner& Owner, FVerifyPutValueState* State, FCacheGetValueResponse&& Response)
{
	FCachePutValueRequest& Request = State->VerifyRequests[int32(Response.UserData)];

	if (Response.Status == EStatus::Ok)
	{
		if (Request.Value.GetRawHash() == Response.Value.GetRawHash())
		{
			UE_LOGF(LogDerivedDataCache, Verbose,
				"Verify: Data in the cache matches newly generated data for %ls from '%ls'.",
				*WriteToString<96>(Request.Key), *Request.Name);
			State->OnComplete(Request.MakeResponse(EStatus::Ok));
			BucketStats.Modify(Response.Key.Bucket, [&Request](FDerivedDataCacheVerificationStat& BucketStat) 
			{
				BucketStat.ValuesMatchedCache++;
				BucketStat.ValuesMatchedCacheBytes += Request.Value.GetRawSize();
			});
		}
		else
		{
			const FCacheGetValueRequest GetDataRequests[]{{Response.Name, Response.Key, ECachePolicy::Default, Response.UserData}};
			return InnerCache->GetValue(GetDataRequests, Owner, [this, &Owner, State](FCacheGetValueResponse&& DataResponse)
			{
				GetDataComplete(Owner, State, MoveTemp(DataResponse));
			});
		}
	}
	else
	{
		UE_LOGF(LogDerivedDataCache, Display,
			"Verify: Cache did not contain a value for %ls from '%ls'.",
			*WriteToString<96>(Request.Key), *Request.Name);
		BucketStats.Modify(Response.Key.Bucket, [&Request](FDerivedDataCacheVerificationStat& BucketStat) 
		{
			BucketStat.ValuesNotInCache++;
			BucketStat.ValuesNotInCacheBytes += Request.Value.GetRawSize();
		});
		FWriteScopeLock Lock(State->Lock);
		State->ForwardRequests.Add(MoveTemp(Request));
	}

	GetComplete(Owner, State);
}

void FCacheStoreVerify::GetDataComplete(IRequestOwner& Owner, FVerifyPutValueState* State, FCacheGetValueResponse&& Response)
{
	FCachePutValueRequest& Request = State->VerifyRequests[int32(Response.UserData)];

	if (Response.Status == EStatus::Ok)
	{
		if (Request.Value.GetRawHash() == Response.Value.GetRawHash())
		{
			UE_LOGF(LogDerivedDataCache, Verbose,
				"Verify: Data in the cache matches newly generated data for %ls from '%ls'.",
				*WriteToString<96>(Request.Key), *Request.Name);
			State->OnComplete(Request.MakeResponse(EStatus::Ok));
			BucketStats.Modify(Response.Key.Bucket, [&Request](FDerivedDataCacheVerificationStat& BucketStat) 
			{
				BucketStat.ValuesMatchedCache++;
				BucketStat.ValuesMatchedCacheBytes += Request.Value.GetRawSize();
			});
		}
		else
		{
			LogChangedValue(Request.Name, Request.Key, FValueId::Null,
				Request.Value.GetRawHash(), Response.Value.GetRawHash(),
				Request.Value.GetData().DecompressToComposite(), Response.Value.GetData().DecompressToComposite());
			BucketStats.Modify(Response.Key.Bucket, [&Request](FDerivedDataCacheVerificationStat& BucketStat) 
			{
				BucketStat.ValuesChanged++;
				BucketStat.ValuesChangedBytes += Request.Value.GetRawSize();
			});
			if (bPutOnError)
			{
				// Ask to overwrite existing values to potentially eliminate the mismatch.
				UE_LOGF(LogDerivedDataCache, Display,
					"Verify: Writing newly generated data to the cache for %ls from '%ls'.",
					*WriteToString<96>(Request.Key), *Request.Name);
				Request.Policy &= ~ECachePolicy::Query;
				FWriteScopeLock Lock(State->Lock);
				State->ForwardRequests.Add(MoveTemp(Request));
			}
			else
			{
				State->OnComplete(Request.MakeResponse(EStatus::Ok));
			}
		}
	}
	else
	{
		UE_LOGF(LogDerivedDataCache, Display,
			"Verify: Cache did not contain a value for %ls from '%ls'.",
			*WriteToString<96>(Request.Key), *Request.Name);
		BucketStats.Modify(Response.Key.Bucket, [&Request](FDerivedDataCacheVerificationStat& BucketStat) 
		{
			BucketStat.ValuesNotInCache++;
			BucketStat.ValuesNotInCacheBytes += Request.Value.GetRawSize();
		});
		FWriteScopeLock Lock(State->Lock);
		State->ForwardRequests.Add(MoveTemp(Request));
	}

	GetComplete(Owner, State);
}

void FCacheStoreVerify::GetComplete(IRequestOwner& Owner, FVerifyPutValueState* State)
{
	if (FWriteScopeLock Lock(State->Lock); --State->ActiveRequests > 0)
	{
		return;
	}
	if (!State->ForwardRequests.IsEmpty())
	{
		InnerCache->PutValue(State->ForwardRequests, Owner, MoveTemp(State->OnComplete));
	}
	delete State;
}

void FCacheStoreVerify::GetValue(
	const TConstArrayView<FCacheGetValueRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetValueComplete&& OnComplete)
{
	TArray<FCacheGetValueRequest, TInlineAllocator<8>> ForwardRequests;
	TArray<FCacheGetValueRequest, TInlineAllocator<8>> VerifyRequests;
	ForwardRequests.Reserve(Requests.Num());
	VerifyRequests.Reserve(Requests.Num());
	{
		FScopeLock Lock(&AlreadyTestedLock);
		for (const FCacheGetValueRequest& Request : Requests)
		{
			const bool bForward = EnumHasAnyFlags(Request.Policy, ECachePolicy::NonDeterministic) ||
				!KeyFilter.IsMatch(Request.Key) || AlreadyTested.Contains(Request.Key);
			(bForward ? ForwardRequests : VerifyRequests).Add(Request);
		}
	}

	CompleteWithStatus(VerifyRequests, OnComplete, EStatus::Error);

	if (!ForwardRequests.IsEmpty())
	{
		InnerCache->GetValue(ForwardRequests, Owner, MoveTemp(OnComplete));
	}
}

void FCacheStoreVerify::GetChunks(
	const TConstArrayView<FCacheGetChunkRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetChunkComplete&& OnComplete)
{
	TArray<FCacheGetChunkRequest, TInlineAllocator<8>> ForwardRequests;
	TArray<FCacheGetChunkRequest, TInlineAllocator<8>> VerifyRequests;
	ForwardRequests.Reserve(Requests.Num());
	VerifyRequests.Reserve(Requests.Num());
	{
		FScopeLock Lock(&AlreadyTestedLock);
		for (const FCacheGetChunkRequest& Request : Requests)
		{
			const bool bForward = EnumHasAnyFlags(Request.Policy, ECachePolicy::NonDeterministic) ||
				!KeyFilter.IsMatch(Request.Key) || AlreadyTested.Contains(Request.Key);
			(bForward ? ForwardRequests : VerifyRequests).Add(Request);
		}
	}

	CompleteWithStatus(VerifyRequests, OnComplete, EStatus::Error);

	if (!ForwardRequests.IsEmpty())
	{
		InnerCache->GetChunks(ForwardRequests, Owner, MoveTemp(OnComplete));
	}
}

bool FCacheStoreVerify::CompareRecords(
	const FCacheRecord& PutRecord,
	const FCacheRecord& GetRecord,
	const FCacheRecordPolicy& Policy,
	const FSharedString& Name)
{
	bool bEqual = true;

	const FCacheKey& Key = PutRecord.GetKey();
	const TConstArrayView<FValueWithId> PutValues = PutRecord.GetValues();
	const TConstArrayView<FValueWithId> GetValues = GetRecord.GetValues();
	const FValueWithId* PutIt = PutValues.GetData();
	const FValueWithId* GetIt = GetValues.GetData();
	const FValueWithId* const PutEnd = PutIt + PutValues.Num();
	const FValueWithId* const GetEnd = GetIt + GetValues.Num();
	uint32 ValuesDifferent = 0, ValuesMatch = 0, NewValues = 0, MissingValues = 0;
	uint64 ValuesDifferentBytes = 0, ValuesMatchBytes = 0, NewValuesBytes = 0, MissingValuesBytes = 0;

	const auto LogNewValue = [&Name, &Key, &NewValues, &NewValuesBytes](const FValueWithId& Value)
	{
		UE_LOGF(LogDerivedDataCache, Error,
			"Verify: Value %ls with hash %ls is in the new record but does not exist in the cache for %ls from '%ls'.",
			*WriteToString<32>(Value.GetId()), *WriteToString<48>(Value.GetRawHash()), *WriteToString<96>(Key), *Name);
		NewValues++;
		NewValuesBytes += Value.GetRawSize();
	};

	const auto LogOldValue = [&Name, &Key, &MissingValues, &MissingValuesBytes](const FValueWithId& Value)
	{
		UE_LOGF(LogDerivedDataCache, Error,
			"Verify: Value %ls with hash %ls is in the cache but does not exist in the new record for %ls from '%ls'.",
			*WriteToString<32>(Value.GetId()), *WriteToString<48>(Value.GetRawHash()), *WriteToString<96>(Key), *Name);
		MissingValues++;
		MissingValuesBytes += Value.GetRawSize();
	};

	while (PutIt != PutEnd && GetIt != GetEnd)
	{
		if (PutIt->GetId() == GetIt->GetId())
		{
			if (PutIt->GetRawHash() != GetIt->GetRawHash())
			{
				if (!EnumHasAnyFlags(Policy.GetValuePolicy(PutIt->GetId()), ECachePolicy::NonDeterministic))
				{
					LogChangedValue(Name, Key, PutIt->GetId(),
						PutIt->GetRawHash(), GetIt->GetRawHash(),
						PutIt->GetData().DecompressToComposite(), GetIt->GetData().DecompressToComposite());
					bEqual = false;
					ValuesDifferent++;
					ValuesDifferentBytes += PutIt->GetRawSize();
				}
				else
				{
					UE_LOGF(LogDerivedDataCache, Verbose,
						"Verify: Value %ls has hash %ls in the newly generated data and hash %ls in the cache for %ls from '%ls'. "
							 "The value is expected to be non-deterministic.",
						*WriteToString<32>(PutIt->GetId()), *WriteToString<48>(PutIt->GetRawHash()),
						*WriteToString<48>(GetIt->GetRawHash()), *WriteToString<96>(Key), *Name);
					ValuesMatch++;
					ValuesMatchBytes += PutIt->GetRawSize();
				}
			}
			else
			{
				ValuesMatch++;
				ValuesMatchBytes += PutIt->GetRawSize();
			}
			++PutIt;
			++GetIt;
		}
		else if (PutIt->GetId() < GetIt->GetId())
		{
			LogNewValue(*PutIt++);
			bEqual = false;
		}
		else
		{
			LogOldValue(*GetIt++);
			bEqual = false;
		}
	}

	while (PutIt != PutEnd)
	{
		LogNewValue(*PutIt++);
		bEqual = false;
	}

	while (GetIt != GetEnd)
	{
		LogOldValue(*GetIt++);
		bEqual = false;
	}

	BucketStats.Modify(PutRecord.GetKey().Bucket, [&](FDerivedDataCacheVerificationStat& BucketStat) 
	{
		BucketStat.RecordsMatchedCache += bEqual ? 1 : 0;
		BucketStat.ValuesMatchedCache += ValuesMatch;
		BucketStat.ValuesMatchedCacheBytes += ValuesMatchBytes;
		BucketStat.ValuesNotInCache += NewValues;
		BucketStat.ValuesNotInCacheBytes += NewValuesBytes;
		BucketStat.ValuesChanged += ValuesDifferent;
		BucketStat.ValuesChangedBytes += ValuesDifferentBytes;
		BucketStat.ValuesNotInRequest += MissingValues;
		BucketStat.ValuesNotInRequestBytes += MissingValuesBytes;
	});

	return bEqual;
}

void FCacheStoreVerify::LogChangedValue(
	const FSharedString& Name,
	const FCacheKey& Key,
	const FValueId& Id,
	const FIoHash& NewRawHash,
	const FIoHash& OldRawHash,
	const FCompositeBuffer& NewRawData,
	const FCompositeBuffer& OldRawData)
{
	TStringBuilder<32> IdString;
	if (Id.IsValid())
	{
		IdString << TEXT(' ') << Id;
	}

	const auto LogDataToFile = [&Name, &Key, &Id, &IdString](const FIoHash& RawHash, const FCompositeBuffer& RawData, FStringView Extension)
	{
		if (!RawData.IsNull())
		{
			TStringBuilder<256> Path;
			FPathViews::Append(Path, FPaths::ProjectSavedDir(), TEXT("VerifyDDC"), TEXT(""));
			Path << Key.Bucket << TEXT('_') << Key.Hash;
			if (Id.IsValid())
			{
				Path << TEXT('_') << Id;
			}
			Path << Extension;
			if (TUniquePtr<FArchive> Ar{IFileManager::Get().CreateFileWriter(*Path, FILEWRITE_Silent)})
			{
				for (const FSharedBuffer& Segment : RawData.GetSegments())
				{
					Ar->Serialize(const_cast<void*>(Segment.GetData()), int64(Segment.GetSize()));
				}
			}
		}
		else
		{
			UE_LOGF(LogDerivedDataCache, Log,
				"Verify: Value%ls does not have data with hash %ls to save to disk for %ls from '%ls'.",
				*IdString, *WriteToString<48>(RawHash), *WriteToString<96>(Key), *Name);
		}
	};

	UE_LOGF(LogDerivedDataCache, Error,
		"Verify: Value%ls has hash %ls in the newly generated data and hash %ls in the cache for %ls from '%ls'.",
		*IdString, *WriteToString<48>(NewRawHash), *WriteToString<48>(OldRawHash), *WriteToString<96>(Key), *Name);
	LogDataToFile(NewRawHash, NewRawData, TEXTVIEW(".verify"));
	LogDataToFile(OldRawHash, OldRawData, TEXTVIEW(".fromcache"));
}

void FCacheStoreVerify::GatherVerificationStats(FDerivedDataCacheVerificationStats& OutStats) const
{
	// Buckets may share the same display name, combine their stats here
	TUniqueLock DoLock(BucketStats.Lock);
	for (const TPair<FCacheBucket, FDerivedDataCacheVerificationStat>& Entry : BucketStats.PerBucketStats)
	{
		TStringBuilder<64> BucketName;
		Entry.Key.ToDisplayName(BucketName);
		FDerivedDataCacheVerificationStat* StatToRecord = Algo::FindBy(OutStats.Entries, BucketName.ToView(), &FDerivedDataCacheVerificationStat::BucketName);
		if (StatToRecord != nullptr)
		{
			*StatToRecord += Entry.Value;
		}
		else
		{
			FDerivedDataCacheVerificationStat& NewEntry = OutStats.Entries.Emplace_GetRef(Entry.Value);
			NewEntry.BucketName = BucketName;
		}
	}
}

ILegacyCacheStore* CreateCacheStoreVerify(ILegacyCacheStore* InnerCache, bool bPutOnError)
{
	return new FCacheStoreVerify(InnerCache, bPutOnError);
}

} // UE::DerivedData
