// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DerivedDataCacheStore.h"
#include "DerivedDataLegacyCacheStore.h"
#include "Templates/UniquePtr.h"

namespace UE::DerivedData
{

/**
 * Base class for cache store proxies.
 *
 * A cache store proxy is a cache store that modifies the behavior of another cache store.
 * Use a derived class as the owner when constructing the inner cache store.
 */
class FCacheStoreProxy : public ILegacyCacheStore, public ICacheStoreOwner
{
public:
	explicit FCacheStoreProxy(ICacheStoreOwner& InOuterOwner)
		: OuterOwner(InOuterOwner)
	{
	}

	void Add(const FSharedString& Name, ILegacyCacheStore* CacheStore, ECacheStoreFlags Flags) override
	{
		check(!InnerStore);
		InnerStore.Reset(CacheStore);
		InnerStoreName = Name;
		OuterOwner.Add(Name, this, Flags);
	}

	void SetFlags(ILegacyCacheStore* CacheStore, ECacheStoreFlags Flags) override
	{
		check(CacheStore == InnerStore.Get());
		OuterOwner.SetFlags(this, Flags);
	}

	void RemoveNotSafe(ILegacyCacheStore* CacheStore) override
	{
		check(CacheStore == InnerStore.Get());
		OuterOwner.RemoveNotSafe(this);
		InnerStore.Reset();
		InnerStoreName.Reset();
	}

	bool HasAllFlags(ECacheStoreFlags Flags) const override
	{
		return OuterOwner.HasAllFlags(Flags);
	}

	void AddMaintainer(ICacheStoreMaintainer* Maintainer) override
	{
		OuterOwner.AddMaintainer(Maintainer);
	}

	void RemoveMaintainer(ICacheStoreMaintainer* Maintainer) override
	{
		OuterOwner.RemoveMaintainer(Maintainer);
	}

	int32 AddToAsyncTaskCounter(int32 Value) override
	{
		return OuterOwner.AddToAsyncTaskCounter(Value);
	}

	ICacheStoreStats* CreateStats(ILegacyCacheStore* CacheStore, ECacheStoreFlags Flags, FStringView Type, FStringView Name, FStringView Path) override
	{
		check(CacheStore == InnerStore.Get());
		return OuterOwner.CreateStats(this, Flags, Type, Name, Path);
	}

	void DestroyStats(ICacheStoreStats* Stats) override
	{
		OuterOwner.DestroyStats(Stats);
	}

	void LegacyResourceStats(TArray<FDerivedDataCacheResourceStat>& OutStats) const override
	{
		OuterOwner.LegacyResourceStats(OutStats);
	}

	void SetShuttingDown() override
	{
		OuterOwner.SetShuttingDown();
	}

	bool IsShuttingDown() const override
	{
		return OuterOwner.IsShuttingDown();
	}

	void WaitForIdle() const override
	{
		OuterOwner.WaitForIdle();
	}

protected:
	ILegacyCacheStore* GetInnerStore() const
	{
		check(InnerStore);
		return InnerStore.Get();
	}

	const FSharedString& GetInnerStoreName() const
	{
		check(InnerStore);
		return InnerStoreName;
	}

	void DestroyInnerStore()
	{
		InnerStore.Reset();
		InnerStoreName.Reset();
	}

private:
	ICacheStoreOwner& OuterOwner;
	TUniquePtr<ILegacyCacheStore> InnerStore;
	FSharedString InnerStoreName;
};

} // UE::DerivedData
