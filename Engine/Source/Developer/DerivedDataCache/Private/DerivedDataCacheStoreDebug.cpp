// Copyright Epic Games, Inc. All Rights Reserved.

#include "Async/Mutex.h"
#include "Async/SharedLock.h"
#include "Async/SharedMutex.h"
#include "Async/UniqueLock.h"
#include "Containers/Map.h"
#include "DerivedDataCacheKeyFilter.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataCacheStoreProxy.h"
#include "DerivedDataPrivate.h"
#include "Logging/LogMacros.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "String/Find.h"

namespace UE::DerivedData
{

/**
 * A cache store that simulates misses for debugging and testing purposes.
 */
class FCacheStoreDebug final : public FCacheStoreProxy
{
public:
	static FCacheStoreDebug* TryCreate(const TCHAR* Name, ICacheStoreOwner& Owner)
	{
		FCacheKeyFilter Filter;
		if (ParseFilter(Filter, Name, FCommandLine::Get()) || ParseDefaultFilter(Filter))
		{
			return new FCacheStoreDebug(Owner, MoveTemp(Filter));
		}
		return nullptr;
	}

	~FCacheStoreDebug()
	{
		// Destroy the inner store before our members are destroyed.
		DestroyInnerStore();
	}

	// ICacheStore Interface

	void Put(
		TConstArrayView<FCachePutRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutComplete&& OnComplete) final
	{
		for (const FCachePutRequest& Request : Requests)
		{
			ShouldSimulatePutMiss(Request.Record.GetKey());
		}
		GetInnerStore()->Put(Requests, Owner, MoveTemp(OnComplete));
	}

	void Get(
		TConstArrayView<FCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetComplete&& OnComplete) final
	{
		TArray<FCacheGetRequest, TInlineAllocator<1>> ForwardRequests;
		ForwardRequests.Reserve(Requests.Num());

		for (const FCacheGetRequest& Request : Requests)
		{
			if (ShouldSimulateGetMiss(Request.Key))
			{
				UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Simulated miss for get of %s from '%ls'",
					*GetInnerStoreName(), *WriteToAnsiString<96>(Request.Key), *Request.Name);
				AddMissStats(Request.Name, Request.Key.Bucket, ECacheStoreRequestType::Record, ECacheStoreRequestOp::Get);
				OnComplete(Request.MakeResponse(EStatus::Error));
			}
			else
			{
				ForwardRequests.Add(Request);
			}
		}

		if (!ForwardRequests.IsEmpty())
		{
			GetInnerStore()->Get(ForwardRequests, Owner, MoveTemp(OnComplete));
		}
	}

	void PutValue(
		TConstArrayView<FCachePutValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutValueComplete&& OnComplete) final
	{
		for (const FCachePutValueRequest& Request : Requests)
		{
			ShouldSimulatePutMiss(Request.Key);
		}
		GetInnerStore()->PutValue(Requests, Owner, MoveTemp(OnComplete));
	}

	void GetValue(
		TConstArrayView<FCacheGetValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetValueComplete&& OnComplete) final
	{
		TArray<FCacheGetValueRequest, TInlineAllocator<1>> ForwardRequests;
		ForwardRequests.Reserve(Requests.Num());

		for (const FCacheGetValueRequest& Request : Requests)
		{
			if (ShouldSimulateGetMiss(Request.Key))
			{
				UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Simulated miss for get of %s from '%ls'",
					*GetInnerStoreName(), *WriteToAnsiString<96>(Request.Key), *Request.Name);
				AddMissStats(Request.Name, Request.Key.Bucket, ECacheStoreRequestType::Value, ECacheStoreRequestOp::Get);
				OnComplete(Request.MakeResponse(EStatus::Error));
			}
			else
			{
				ForwardRequests.Add(Request);
			}
		}

		if (!ForwardRequests.IsEmpty())
		{
			GetInnerStore()->GetValue(ForwardRequests, Owner, MoveTemp(OnComplete));
		}
	}

	void GetChunks(
		TConstArrayView<FCacheGetChunkRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetChunkComplete&& OnComplete) final
	{
		TArray<FCacheGetChunkRequest, TInlineAllocator<1>> ForwardRequests;
		ForwardRequests.Reserve(Requests.Num());

		for (const FCacheGetChunkRequest& Request : Requests)
		{
			if (ShouldSimulateGetMiss(Request.Key))
			{
				UE_LOGF(LogDerivedDataCache, Verbose, "%ls: Simulated miss for get of %s from '%ls'",
					*GetInnerStoreName(), *WriteToAnsiString<96>(Request.Key), *Request.Name);
				AddMissStats(Request.Name, Request.Key.Bucket, Request.Id.IsValid() ? ECacheStoreRequestType::Record : ECacheStoreRequestType::Value, ECacheStoreRequestOp::GetChunk);
				OnComplete(Request.MakeResponse(EStatus::Error));
			}
			else
			{
				ForwardRequests.Add(Request);
			}
		}

		if (!ForwardRequests.IsEmpty())
		{
			GetInnerStore()->GetChunks(ForwardRequests, Owner, MoveTemp(OnComplete));
		}
	}

	// ILegacyCacheStore Interface

	void LegacyStats(FDerivedDataCacheStatsNode& OutNode) final
	{
		GetInnerStore()->LegacyStats(OutNode);
	}

	// ICacheStoreOwner Interface

	ICacheStoreStats* CreateStats(ILegacyCacheStore* CacheStore, ECacheStoreFlags Flags, FStringView Type, FStringView Name, FStringView Path) final
	{
		ICacheStoreStats* Stats = FCacheStoreProxy::CreateStats(CacheStore, Flags, Type, Name, Path);
		TUniqueLock Lock(StoreStatsMutex);
		StoreStats.Add(Stats);
		return Stats;
	}

	void DestroyStats(ICacheStoreStats* Stats) final
	{
		{
			TUniqueLock Lock(StoreStatsMutex);
			verify(StoreStats.RemoveSingle(Stats) == 1);
		}
		FCacheStoreProxy::DestroyStats(Stats);
	}

private:
	enum class EKeyState
	{
		None,
		HitGet,
		MissGet,
	};

	explicit FCacheStoreDebug(ICacheStoreOwner& InOuterOwner, FCacheKeyFilter&& InFilter)
		: FCacheStoreProxy(InOuterOwner)
		, Filter(MoveTemp(InFilter))
	{
	}

	static bool ParseFilter(FCacheKeyFilter& OutFilter, const TCHAR* NodeName, const TCHAR* Tokens)
	{
		// Check if the input stream has any DDC options for this node.
		TStringBuilder<64> Prefix(InPlace, TEXTVIEW("-DDC-"), NodeName, '-');
		if (String::FindFirst(Tokens, Prefix, ESearchCase::IgnoreCase) == INDEX_NONE)
		{
			return false;
		}

		// Look for -DDC-Local-MissRate=, -DDC-Shared-MissRate=, etc.
		float MissRate = 0.0f;
		FParse::Value(Tokens, *WriteToString<64>(Prefix, TEXTVIEW("MissRate=")), MissRate);

		// Look for -DDC-Local-MissTypes=AnimSeq+Audio, -DDC-Shared-MissTypes=AnimSeq+Audio, etc.
		// Look for -DDC-Local-MissKeys=StaticMesh/<Hash>+Texture/<Hash>, -DDC-Shared-MissKeys=StaticMesh/<Hash>+Texture/<Hash>, etc.
		OutFilter = FCacheKeyFilter::Parse(Tokens,
			*WriteToString<64>(Prefix, TEXTVIEW("MissTypes=")),
			*WriteToString<64>(Prefix, TEXTVIEW("MissKeys=")), MissRate);

		// Look for -DDC-Local-MissSalt=, -DDC-Shared-MissSalt=, etc.
		uint32 Salt = 0;
		if (FParse::Value(Tokens, *WriteToString<64>(Prefix, TEXTVIEW("MissSalt=")), Salt))
		{
			OutFilter.SetSalt(Salt);
		}

		UE_CLOGF(OutFilter.RequiresSalt(), LogDerivedDataCache, Display,
			"%ls: Using salt %lsMissSalt=%u to filter cache keys to simulate misses on.",
			NodeName, *Prefix, OutFilter.GetSalt());

		return true;
	}

	static bool ParseDefaultFilter(FCacheKeyFilter& OutFilter)
	{
		static FCacheKeyFilter DefaultFilter;
		static bool bHasDefaultFilter = ParseFilter(DefaultFilter, TEXT("All"), FCommandLine::Get());
		OutFilter = DefaultFilter;
		return bHasDefaultFilter;
	}

	bool ShouldSimulatePutMiss(const FCacheKey& Key)
	{
		if (!Filter.IsMatch(Key))
		{
			return false;
		}

		const uint32 KeyHash = GetTypeHash(Key);
		TUniqueLock Lock(KeyStatesMutex);
		KeyStates.AddByHash(KeyHash, Key, EKeyState::HitGet);
		return false;
	}

	bool ShouldSimulateGetMiss(const FCacheKey& Key)
	{
		if (!Filter.IsMatch(Key))
		{
			return false;
		}

		const uint32 KeyHash = GetTypeHash(Key);
		TUniqueLock Lock(KeyStatesMutex);
		return KeyStates.FindOrAddByHash(KeyHash, Key, EKeyState::MissGet) == EKeyState::MissGet;
	}

	void AddMissStats(const FSharedString& Name, const FCacheBucket& Bucket, ECacheStoreRequestType Type, ECacheStoreRequestOp Op)
	{
		TSharedLock Lock(StoreStatsMutex);
		if (!StoreStats.IsEmpty())
		{
			FRequestStats RequestStats;
			RequestStats.Name = Name;
			RequestStats.Bucket = Bucket;
			RequestStats.Type = Type;
			RequestStats.Op = Op;
			RequestStats.Status = EStatus::Error;
			for (ICacheStoreStats* Stats : StoreStats)
			{
				Stats->AddRequest(RequestStats);
			}
		}
	}

	FCacheKeyFilter Filter;
	TArray<ICacheStoreStats*> StoreStats;
	mutable TMap<FCacheKey, EKeyState> KeyStates;
	mutable FSharedMutex StoreStatsMutex;
	mutable FMutex KeyStatesMutex;
};

ICacheStoreOwner* CreateCacheStoreDebug(const TCHAR* Name, const TCHAR* Config, ICacheStoreOwner& Owner)
{
	return FCacheStoreDebug::TryCreate(Name, Owner);
}

} // UE::DerivedData
