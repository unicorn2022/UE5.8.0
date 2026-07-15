// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "MetaHumanMassActorAnimationProcessor.generated.h"

#define UE_API METAHUMANCROWD_API

/**
 * Mass processor that drives animation state for MetaHuman crowd entities on the Actor/Blueprint path.
 * Runs only for entities that have a spawned actor that implements IMetahumanMassCrowdActorBlueprintInterface
 * Game-thread only by default. Change bRequiresGameThreadExecution in INI if you're sure your logic 
 * is thread-safe/doesn't run in parallel with the animation update/double buffer parameters/etc.
 */
UCLASS(MinimalAPI)
class UMetaHumanMassActorProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UE_API UMetaHumanMassActorProcessor();

protected:

	/** Configure the owned FMassEntityQuery instances to express processor's requirements */
	UE_API virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;

	/**
	* Execution method for this processor
	* @param EntityManager is the system to execute the lambdas on each entity chunk
	* @param Context is the execution context to be passed when executing the lambdas */
	UE_API virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};

#undef UE_API
