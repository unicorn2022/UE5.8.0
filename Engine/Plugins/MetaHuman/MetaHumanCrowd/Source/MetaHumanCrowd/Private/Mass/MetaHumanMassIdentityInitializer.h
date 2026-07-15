// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityQuery.h"
#include "MassObserverProcessor.h"
#include "MetaHumanMassIdentityInitializer.generated.h"

#define UE_API METAHUMANCROWD_API

/**
 * Processor for initializing MetaHuman appearance.
 */
UCLASS(MinimalAPI)
class UMetaHumanMassIdentityInitializer : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UE_API UMetaHumanMassIdentityInitializer();

protected:
	UE_API virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	UE_API virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};

#undef UE_API
