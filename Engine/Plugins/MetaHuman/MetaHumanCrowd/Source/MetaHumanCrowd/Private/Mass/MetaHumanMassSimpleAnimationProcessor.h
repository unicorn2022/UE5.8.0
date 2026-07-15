// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "MetaHumanMassSimpleAnimationProcessor.generated.h"

#define UE_API METAHUMANCROWD_API

UCLASS(MinimalAPI)
class UMetaHumanMassSimpleAnimationProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UE_API UMetaHumanMassSimpleAnimationProcessor();

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
