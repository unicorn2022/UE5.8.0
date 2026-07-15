// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassNavigationTypes.h"
#include "MassSmartObjectRequest.h"
#include "MassStateTreeTypes.h"
#include "SmartObjectRuntime.h"
#include "SmartObjectSubsystem.h"
#include "StateTreePropertyRef.h"
#include "MetaHumanMassSmartObjectFurthestCandidateTask.generated.h"

struct FTransformFragment;
struct FAgentRadiusFragment;
struct FAgentHeightFragment;
class USmartObjectSubsystem;

USTRUCT()
struct FMetaHumanMassSmartObjectFurthestCandidateTaskInstanceData
{
	GENERATED_BODY()

	/**
	 * Candidate slots from a prior FindSmartObject task. The task will pick the furthest
	 * slot from the entity's current location. No claim is made.
	 */
	UPROPERTY(VisibleAnywhere, Category = Input, meta = (RefType = "/Script/MassSmartObjects.MassSmartObjectCandidateSlots"))
	FStateTreePropertyRef CandidateSlots;

	/**
	 * When using the entrance location request with selection method 'NearestToSearchLocation',
	 * this property indicates whether the request will set the search location using
	 * the entity transform or use the request value (can be bound).
	 */
	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bUseEntityLocationAsSearchLocation = true;

	/** Request parameters when using the entrance location request. */
	UPROPERTY(EditAnywhere, Category = Parameter)
	FSmartObjectSlotEntranceLocationRequest EntranceRequest;

	UPROPERTY(EditAnywhere, Category = Output)
	FMassTargetLocation SmartObjectLocation;
};

/**
 * Picks the furthest Smart Object slot from a set of candidates and resolves its navigation target,
 * without claiming the slot. Suitable for navigating toward a Smart Object as an unclaimed waypoint.
 *
 * Intended to be used after a FindSmartObject task that populates CandidateSlots.
 */
USTRUCT(meta = (DisplayName = "MetaHuman Navigate To Furthest Smart Object Candidate (No Claim)"))
struct FMetaHumanMassSmartObjectFurthestCandidateTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FMetaHumanMassSmartObjectFurthestCandidateTaskInstanceData;

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

	TStateTreeExternalDataHandle<FAgentRadiusFragment, EStateTreeExternalDataRequirement::Optional> AgentRadiusHandle;
	TStateTreeExternalDataHandle<FAgentHeightFragment, EStateTreeExternalDataRequirement::Optional> AgentHeightHandle;
	TStateTreeExternalDataHandle<FTransformFragment> TransformHandle;
	TStateTreeExternalDataHandle<USmartObjectSubsystem> SmartObjectSubsystemHandle;

	/**
	 * Whether to use the entrance location request first. Falls back to raw slot transform
	 * if no entrance is found or the request is disabled.
	 */
	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bUseEntranceLocationRequest = true;
};
