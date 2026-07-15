// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanMassSmartObjectFurthestCandidateTask.h"

#include "MassAIBehaviorTypes.h"
#include "MassCommonFragments.h"
#include "MassNavigationFragments.h"
#include "MassSmartObjectRequest.h"
#include "MassStateTreeExecutionContext.h"
#include "NavigationSystem.h"
#include "SmartObjectSubsystem.h"
#include "StateTreeLinker.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanMassSmartObjectFurthestCandidateTask)

bool FMetaHumanMassSmartObjectFurthestCandidateTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(TransformHandle);
	Linker.LinkExternalData(AgentRadiusHandle);
	Linker.LinkExternalData(AgentHeightHandle);
	Linker.LinkExternalData(SmartObjectSubsystemHandle);

	return true;
}

EStateTreeRunStatus FMetaHumanMassSmartObjectFurthestCandidateTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalData(SmartObjectSubsystemHandle);
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	InstanceData.SmartObjectLocation.Reset();

	// Resolve the candidate slots from the property ref.
	const FMassSmartObjectCandidateSlots* CandidateSlots = InstanceData.CandidateSlots.GetMutablePtr<FMassSmartObjectCandidateSlots>(Context);
	if (CandidateSlots == nullptr)
	{
		MASSBEHAVIOR_LOG(Error, TEXT("FMetaHumanMassSmartObjectFurthestCandidateTask: CandidateSlots property ref is not set."));
		return EStateTreeRunStatus::Failed;
	}

	if (CandidateSlots->NumSlots == 0)
	{
		MASSBEHAVIOR_LOG(Log, TEXT("FMetaHumanMassSmartObjectFurthestCandidateTask: No candidate slots available."));
		return EStateTreeRunStatus::Failed;
	}

	const FVector EntityLocation = Context.GetExternalData(TransformHandle).GetTransform().GetLocation();
	const FAgentRadiusFragment* AgentRadius = Context.GetExternalDataPtr(AgentRadiusHandle);
	const FAgentHeightFragment* AgentHeight = Context.GetExternalDataPtr(AgentHeightHandle);

	// Resolve nav system and agent properties once for reachability testing.
	UNavigationSystemV1* NavSys = Cast<UNavigationSystemV1>(Context.GetWorld()->GetNavigationSystem());
	const ANavigationData* NavData = nullptr;
	if (NavSys && AgentRadius && AgentHeight)
	{
		const FNavAgentProperties NavAgentProperties(AgentRadius->Radius, AgentHeight->Height);
		NavData = NavSys->GetNavDataForProps(NavAgentProperties, EntityLocation);
	}

	// Pick the reachable slot whose world transform is furthest from the entity.
	FSmartObjectSlotHandle BestSlotHandle;
	float BestDistanceSq = -1.f;

	for (int32 SlotIndex = 0; SlotIndex < CandidateSlots->NumSlots; ++SlotIndex)
	{
		const FSmartObjectCandidateSlot& Candidate = CandidateSlots->Slots[SlotIndex];
		const FSmartObjectSlotHandle SlotHandle = Candidate.Result.SlotHandle;

		if (!SlotHandle.IsValid())
		{
			continue;
		}

		const TOptional<FTransform> SlotTransform = SmartObjectSubsystem.GetSlotTransform(SlotHandle);
		if (!SlotTransform.IsSet())
		{
			continue;
		}

		const FVector SlotLocation = SlotTransform.GetValue().GetLocation();

		// Skip slots that aren't reachable on the nav mesh.
		if (NavSys && NavData)
		{
			FPathFindingQuery Query(nullptr, *NavData, EntityLocation, SlotLocation);
			Query.SetAllowPartialPaths(false);
			if (!NavSys->TestPathSync(Query))
			{
				continue;
			}
		}

		const float DistSq = FVector::DistSquared(SlotLocation, EntityLocation);
		if (DistSq > BestDistanceSq)
		{
			BestDistanceSq = DistSq;
			BestSlotHandle = SlotHandle;
		}
	}

	if (!BestSlotHandle.IsValid())
	{
		MASSBEHAVIOR_LOG(Warning, TEXT("FMetaHumanMassSmartObjectFurthestCandidateTask: No reachable slot found among %d candidates."), CandidateSlots->NumSlots);
		return EStateTreeRunStatus::Failed;
	}

	// Resolve the navigation target from the chosen slot — no claim made.
	if (bUseEntranceLocationRequest)
	{
		FSmartObjectSlotEntranceLocationRequest Request = InstanceData.EntranceRequest;

		if (Request.bProjectNavigationLocation)
		{
			if (NavSys && NavData)
			{
				Request.NavigationData = NavData;
			}
			else
			{
				MASSBEHAVIOR_LOG(Error, TEXT("FMetaHumanMassSmartObjectFurthestCandidateTask: Cannot project navigation location — no NavData or agent fragments available."));
				return EStateTreeRunStatus::Failed;
			}
		}

		if (InstanceData.bUseEntityLocationAsSearchLocation)
		{
			Request.SearchLocation = EntityLocation;
		}

		FSmartObjectSlotEntranceLocationResult EntryLocation;
		if (SmartObjectSubsystem.FindEntranceLocationForSlot(BestSlotHandle, Request, EntryLocation))
		{
			InstanceData.SmartObjectLocation.EndOfPathIntent = EMassMovementAction::Stand;
			InstanceData.SmartObjectLocation.EndOfPathPosition = EntryLocation.Location;
			InstanceData.SmartObjectLocation.EndOfPathRotation = EntryLocation.Rotation.Quaternion();
			return EStateTreeRunStatus::Running;
		}
	}

	// Fall back to raw slot transform.
	const FTransform SlotTransform = SmartObjectSubsystem.GetSlotTransform(BestSlotHandle).Get(FTransform::Identity);

	InstanceData.SmartObjectLocation.EndOfPathIntent = EMassMovementAction::Stand;
	InstanceData.SmartObjectLocation.EndOfPathPosition = SlotTransform.GetLocation();
	InstanceData.SmartObjectLocation.EndOfPathRotation = SlotTransform.GetRotation();

	return EStateTreeRunStatus::Running;
}
