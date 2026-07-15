// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "MetaHumanMassCharacterTrajectoryToActorProcessor.generated.h"

#define UE_API METAHUMANCROWD_API

/**
 * Mass processor that pipes trajectory data to MetaHuman crowd actors via IMetahumanMassCrowdActorBlueprintInterface::SetTrajectory.
 * Runs only for entities that have a spawned actor implementing IMetahumanMassCrowdActorBlueprintInterface.
 * Game-thread only by default. Change bRequiresGameThreadExecution in INI if you're sure your logic 
 * is thread-safe/doesn't run in parallel with the animation update/double buffer parameters/etc.
 */
UCLASS(MinimalAPI)
class UMetaHumanMassCharacterTrajectoryToActorProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UE_API UMetaHumanMassCharacterTrajectoryToActorProcessor();

protected:
	UE_API virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	UE_API virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};

#undef UE_API
