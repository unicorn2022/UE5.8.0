// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityQuery.h"
#include "MassObserverProcessor.h"
#include "MetaHumanMassIdentityDeinitializer.generated.h"

#define UE_API METAHUMANCROWD_API

/**
 * Observer that fires when a MetaHuman crowd entity is destroyed.
 */
UCLASS(MinimalAPI)
class UMetaHumanMassIdentityDeinitializer : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UE_API UMetaHumanMassIdentityDeinitializer();

protected:
	UE_API virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	UE_API virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};

#undef UE_API
