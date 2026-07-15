// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassSpawner.h"
#include "MetaHumanMassSpawner.generated.h"

class AMassSpawner;

/**
 * Implementation of Mass Spawner that allows asset loading to be synchronous.
 */
UCLASS()
class AMetaHumanMassSpawner : public AMassSpawner
{
	GENERATED_BODY()
	
public:
	AMetaHumanMassSpawner();

	/**
	 * If StreamingHandle is set, synchronously waits until all assets are streamed in.
	 */
	UFUNCTION(BlueprintCallable, Category=MassSpawner)
	void WaitForStreamingAssets();

};
