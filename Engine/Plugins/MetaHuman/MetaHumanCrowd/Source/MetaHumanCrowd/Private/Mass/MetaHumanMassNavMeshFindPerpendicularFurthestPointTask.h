// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassNavigationTypes.h"
#include "MassStateTreeTypes.h"
#include "MetaHumanMassNavMeshFindPerpendicularFurthestPointTask.generated.h"

struct FAgentRadiusFragment;
struct FAgentHeightFragment;
struct FStateTreeExecutionContext;
struct FTransformFragment;

USTRUCT()
struct FMetaHumanMassNavMeshFindPerpendicularFurthestPointTaskInstanceData
{
	GENERATED_BODY()

	/**
	 * Reference direction vector. The task will find the furthest nav mesh point along this vector.
	 */
	UPROPERTY(EditAnywhere, Category = Parameters)
	FVector ReferenceDirection = FVector::ForwardVector;

	/** Maximum search distance along the perpendicular directions. */
	UPROPERTY(EditAnywhere, Category = Parameters)
	float MaxSearchDistance = 5000.f;

	/** "None" will result in default filter being used. */
	UPROPERTY(EditAnywhere, Category=Query)
	TSubclassOf<class UNavigationQueryFilter> FilterClass;

	UPROPERTY(EditAnywhere, Category = Output)
	FMassTargetLocation TargetLocation;
};

/**
 * Updates TargetLocation to the furthest reachable point on the NavMesh along a direction
 * directions are tested and the one that reaches furthest is chosen.
 * An optional filter for nav mesh class can be used.
 */
USTRUCT(meta = (DisplayName = "MetaHuman NavMesh Find Furthest Perpendicular Point"))
struct FMetaHumanMassNavMeshFindPerpendicularFurthestPointTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FMetaHumanMassNavMeshFindPerpendicularFurthestPointTaskInstanceData;

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

	TStateTreeExternalDataHandle<FTransformFragment> TransformHandle;
	TStateTreeExternalDataHandle<FAgentRadiusFragment> AgentRadiusHandle;
	TStateTreeExternalDataHandle<FAgentHeightFragment> AgentHeightHandle;
};
