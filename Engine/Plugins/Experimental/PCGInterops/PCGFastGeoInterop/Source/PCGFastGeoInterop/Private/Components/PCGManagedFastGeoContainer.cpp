// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGManagedFastGeoContainer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGManagedFastGeoContainer)

bool UPCGManagedFastGeoContainer::Release(bool bHardRelease, TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGManagedFastGeoContainer::Release);

	if (FastGeo)
	{
		UFastGeoContainer::DestroyRuntime(FastGeo);
		FastGeo = nullptr;
	}

	// Can always remove from PCG Component.
	return true;
}
