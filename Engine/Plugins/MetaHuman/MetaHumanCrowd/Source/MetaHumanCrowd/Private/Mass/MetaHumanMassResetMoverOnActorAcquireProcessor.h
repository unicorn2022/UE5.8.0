// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityQuery.h"
#include "MassProcessor.h"

#include "MetaHumanMassResetMoverOnActorAcquireProcessor.generated.h"

/**
 * Detects entities that have just transitioned into an actor representation
 * (PrevRepresentation was not an actor type, CurrentRepresentation is) and
 * resets the bound actor's UMoverComponent + UMassMoverInputComponent state to
 * match the entity's authoritative Mass fragments. Without this, a pooled
 * actor reused for a new entity carries the previous occupant's residual
 * values for one frame -- the sync state for GenerateMove and the input
 * bridge for ProduceInput -- producing a one-frame capsule pop on the
 * representation swap.
 */
UCLASS()
class UMetaHumanMassResetMoverOnActorAcquireProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMetaHumanMassResetMoverOnActorAcquireProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
