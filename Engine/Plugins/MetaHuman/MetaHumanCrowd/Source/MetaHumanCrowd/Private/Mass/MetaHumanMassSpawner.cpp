// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanMassSpawner.h"

#include "Engine/StreamableManager.h"

AMetaHumanMassSpawner::AMetaHumanMassSpawner()
{
	bAutoSpawnOnBeginPlay = false;
}

void AMetaHumanMassSpawner::WaitForStreamingAssets()
{
	// Wait for asynchronous load requests
	FlushAsyncLoading();

	// Wait for asset streaming to complete, so assets
	// required by the spawner are loaded.
	if (StreamingHandle.IsValid())
	{
		StreamingHandle->WaitUntilComplete();
	}
}