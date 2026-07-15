// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "MetaHumanMassUpdateCustomDataProcessor.generated.h"

#define UE_API METAHUMANCROWD_API

/** Pushes per-appearance custom-data floats onto each visible MetaHuman crowd entity's matching ISKMs every frame. */
UCLASS(MinimalAPI)
class UMetaHumanMassUpdateCustomDataProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UE_API UMetaHumanMassUpdateCustomDataProcessor();

protected:
	UE_API virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	UE_API virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};

#undef UE_API
