// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreTypes.h"
#include "Containers/BitArray.h"
#include "Containers/StringView.h"
#include "DerivedDataCache.h"
#include "DerivedDataLegacyCacheStore.h"
#include "HAL/LowLevelMemTracker.h"
#include "Stats/Stats.h"

class FDerivedDataCacheUsageStats;
class FDerivedDataCacheStatsNode;

DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Gets"),STAT_DDC_NumGets,STATGROUP_DDC, );
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Puts"),STAT_DDC_NumPuts,STATGROUP_DDC, );
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Build"),STAT_DDC_NumBuilds,STATGROUP_DDC, );
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Exists"),STAT_DDC_NumExist,STATGROUP_DDC, );
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("Sync Get Time"),STAT_DDC_SyncGetTime,STATGROUP_DDC, );
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("ASync Wait Time"),STAT_DDC_ASyncWaitTime,STATGROUP_DDC, );
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("Sync Put Time"),STAT_DDC_PutTime,STATGROUP_DDC, );
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("Sync Build Time"),STAT_DDC_SyncBuildTime,STATGROUP_DDC, );
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("Exists Time"),STAT_DDC_ExistTime,STATGROUP_DDC, );

/** Memory allocation tag for internal allocations of DDC backends */
LLM_DECLARE_TAG_API(DDCBackend, DERIVEDDATACACHE_API);

/** Memory allocation tag for allocations made by DDC but that are passed to other systems */
LLM_DECLARE_TAG_API(UntaggedDDCResult, DERIVEDDATACACHE_API);

namespace UE::DerivedData
{

/**
 * Speed classes. Higher values are faster so > / < comparisons make sense.
 */
enum class EBackendSpeedClass
{
	Unknown,		/* Don't know yet*/
	Slow,			/* Slow, likely a remote drive. Some benefit but handle with care */
	Ok,				/* Ok but not great.  */
	Fast,			/* Fast but seek times still have an impact */
	Local			/* Little to no impact from seek times and extremely fast reads */
};

class FDerivedDataBackend : public ICache
{
public:
	static FDerivedDataBackend* CreateStatic();

	/**
	 * Singleton to retrieve the GLOBAL backend
	 *
	 * @return Reference to the global cache backend
	 */
	static FDerivedDataBackend& GetStatic();

	virtual ~FDerivedDataBackend() = default;

	/**
	 * Singleton to retrieve the root cache
	 * @return Reference to the global cache root
	 */
	virtual ILegacyCacheStore& GetRoot() = 0;

	//--------------------
	// System Interface, copied from FDerivedDataCacheInterface
	//--------------------

	virtual void AddToAsyncCompletionCounter(int32 Addend) = 0;
	virtual bool AnyAsyncRequestsRemaining() = 0;
	virtual bool IsShuttingDown() = 0;
	virtual void WaitForQuiescence(bool bShutdown = false) = 0;
	virtual bool GetUsingSharedDDC() const = 0;
	virtual const TCHAR* GetGraphName() const = 0;
	virtual const TCHAR* GetDefaultGraphName() const = 0;

	/**
	 * Mounts a read-only pak file.
	 *
	 * @param PakFilename Pak filename
	 */
	virtual ILegacyCacheStore* MountPakFile(const TCHAR* PakFilename) = 0;

	/**
	 * Unmounts a read-only pak file.
	 *
	 * @param PakFilename Pak filename
	 */
	virtual bool UnmountPakFile(const TCHAR* PakFilename) = 0;

	/**
	 *  Gather the usage of the DDC hierarchically.
	 */
	virtual TSharedRef<FDerivedDataCacheStatsNode> GatherUsageStats() const = 0;

	virtual void GatherResourceStats(TArray<FDerivedDataCacheResourceStat>& DDCResourceStats) const = 0;

	virtual void GatherVerificationStats(FDerivedDataCacheVerificationStats& OutStats) const = 0;
};

/** Lexical conversions from and to enums */

inline const TCHAR* LexToString(EBackendSpeedClass SpeedClass)
{
	switch (SpeedClass)
	{
	case EBackendSpeedClass::Unknown:
		return TEXT("Unknown");
	case EBackendSpeedClass::Slow:
		return TEXT("Slow");
	case EBackendSpeedClass::Ok:
		return TEXT("Ok");
	case EBackendSpeedClass::Fast:
		return TEXT("Fast");
	case EBackendSpeedClass::Local:
		return TEXT("Local");
	}

	checkNoEntry();
	return TEXT("Unknown value! (Update LexToString!)");
}

inline void LexFromString(EBackendSpeedClass& OutValue, const TCHAR* Buffer)
{
	OutValue = EBackendSpeedClass::Unknown;

	if (FCString::Stricmp(Buffer, TEXT("Slow")) == 0)
	{
		OutValue = EBackendSpeedClass::Slow;
	}
	else if (FCString::Stricmp(Buffer, TEXT("Ok")) == 0)
	{
		OutValue = EBackendSpeedClass::Ok;
	}
	else if (FCString::Stricmp(Buffer, TEXT("Fast")) == 0)
	{
		OutValue = EBackendSpeedClass::Fast;
	}
	else if (FCString::Stricmp(Buffer, TEXT("Local")) == 0)
	{
		OutValue = EBackendSpeedClass::Local;
	}
}

} // UE::DerivedData
