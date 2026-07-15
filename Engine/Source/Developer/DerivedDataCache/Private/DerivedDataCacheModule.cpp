// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheModule.h"

#include "Async/RecursiveMutex.h"
#include "Async/UniqueLock.h"
#include "CoreGlobals.h"
#include "DerivedDataBuild.h"
#include "DerivedDataBuildPrivate.h"
#include "DerivedDataCache.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataCachePrivate.h"
#include "DerivedDataPrivate.h"
#include "DerivedDataThreadPoolTask.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/QueuedThreadPool.h"
#include "Modules/ModuleManager.h"

namespace UE::DerivedData::Private
{

FQueuedThreadPool* GCacheThreadPool;

// Implemented in DerivedDataCache.cpp
void CreateCache(ICache*& OutCache, FDerivedDataCacheInterface*& OutLegacyCache);

// Implemented in DerivedDataBackends.cpp
ICache* TryCreateCache(FStringView GraphNameOrConfig);

static FDerivedDataCacheInterface* GDerivedDataLegacyCache;
static ICache* GDerivedDataCache;
static IBuild* GDerivedDataBuild;

class FDerivedDataCacheModule final : public IDerivedDataCacheModule
{
public:
	FDerivedDataCacheInterface* const* CreateOrGetCache() final
	{
		CreateCacheOnce();
		check(GDerivedDataLegacyCache);
		return &GDerivedDataLegacyCache;
	}

	FDerivedDataCacheInterface* const* GetCache() final
	{
		return &GDerivedDataLegacyCache;
	}

	void CreateThreadPoolOnce()
	{
		TUniqueLock Lock(CreateMutex);
		if (!GCacheThreadPool && FPlatformProcess::SupportsMultithreading())
		{
			GCacheThreadPool = FQueuedThreadPool::Allocate();
			const int32 ThreadCount = FPlatformMisc::NumberOfIOWorkerThreadsToSpawn();
		#if WITH_EDITOR
			// Use normal priority in the editor to avoid preempting GT/RT/RHI and other more important threads
			// with CPU processing (i.e. compression) that happens on these IO Threads in the editor.
			verify(GCacheThreadPool->Create(ThreadCount, 96 * 1024, TPri_Normal, TEXT("DDC IO ThreadPool")));
		#else
			verify(GCacheThreadPool->Create(ThreadCount, 96 * 1024, TPri_AboveNormal, TEXT("DDC IO ThreadPool")));
		#endif
		}
	}

	void CreateCacheOnce()
	{
		TUniqueLock Lock(CreateMutex);
		if (!GDerivedDataCache)
		{
			CreateCache(GDerivedDataCache, GDerivedDataLegacyCache);
			check(GDerivedDataCache);
			check(GDerivedDataLegacyCache);
		}
	}

	void CreateBuildOnce()
	{
		TUniqueLock Lock(CreateMutex);
		if (!GDerivedDataBuild)
		{
			GDerivedDataBuild = CreateBuild(GDerivedDataCache);
			check(GDerivedDataBuild);
		}
	}

	void StartupModule() final
	{
		FModuleManager& ModuleManager = FModuleManager::Get();

		// Required to guarantee that SSL shuts down after DDC. Without this, waiting for active
		// cache requests on shutdown can crash when accessing SSL.
		ModuleManager.LoadModuleChecked("SSL");

		// Required to guarantee that DesktopPlatform shuts down after DDC. Without this,
		// waiting for active cache requests on shutdown can fail to reauthenticate.
		// DesktopPlatform is an *optional* dependency and this load may fail.
		ModuleManager.LoadModule("DesktopPlatform");
	}

	void ShutdownModule() final
	{
		delete GDerivedDataBuild;
		GDerivedDataBuild = nullptr;
		delete GDerivedDataLegacyCache;
		GDerivedDataCache = nullptr;
		GDerivedDataLegacyCache = nullptr;
	}

private:
	FRecursiveMutex CreateMutex;
};

static FDerivedDataCacheModule* GetModule()
{
	if (!FPlatformProperties::RequiresCookedData())
	{
		if (IDerivedDataCacheModule* Module = FModuleManager::LoadModulePtr<IDerivedDataCacheModule>("DerivedDataCache"))
		{
			return static_cast<FDerivedDataCacheModule*>(Module);
		}
	}
	return nullptr;
}

void LaunchTaskInCacheThreadPool(IRequestOwner& Owner, TUniqueFunction<void ()>&& TaskBody)
{
	LaunchTaskInThreadPool(0, TEXT("LaunchTaskInCacheThreadPool"), Owner, GCacheThreadPool, MoveTemp(TaskBody));
}

} // UE::DerivedData::Private

namespace UE::DerivedData
{

LLM_DEFINE_TAG(DerivedData);
LLM_DEFINE_TAG(DerivedDataBuild, "Build", "DerivedData");
LLM_DEFINE_TAG(DerivedDataCache, "Cache", "DerivedData");

ICache& GetCache()
{
	if (ICache* Cache = Private::GDerivedDataCache)
	{
		return *Cache;
	}
	check(IsInGameThread());
	if (Private::FDerivedDataCacheModule* Module = Private::GetModule())
	{
		Module->CreateThreadPoolOnce();
		Module->CreateCacheOnce();
	}
	ICache* Cache = Private::GDerivedDataCache;
	checkf(Cache, TEXT("Failed to create derived data cache."));
	return *Cache;
}

ICache* TryGetCache()
{
	return Private::GDerivedDataCache;
}

ICache* CreateCache(FStringView GraphNameOrConfig)
{
	check(IsInGameThread());
	if (Private::FDerivedDataCacheModule* Module = Private::GetModule())
	{
		Module->CreateThreadPoolOnce();
		return Private::TryCreateCache(GraphNameOrConfig);
	}
	return nullptr;
}

IBuild& GetBuild()
{
	if (IBuild* Build = Private::GDerivedDataBuild)
	{
		return *Build;
	}
	check(IsInGameThread());
	if (Private::FDerivedDataCacheModule* Module = Private::GetModule())
	{
		Module->CreateBuildOnce();
	}
	IBuild* Build = Private::GDerivedDataBuild;
	checkf(Build, TEXT("Failed to create derived data build system."));
	return *Build;
}

} // UE::DerivedData

IMPLEMENT_MODULE(UE::DerivedData::Private::FDerivedDataCacheModule, DerivedDataCache);
