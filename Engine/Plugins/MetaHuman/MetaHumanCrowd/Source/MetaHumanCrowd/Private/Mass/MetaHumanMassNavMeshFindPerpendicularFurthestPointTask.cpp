// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanMassNavMeshFindPerpendicularFurthestPointTask.h"
#include "MassAIBehaviorTypes.h"
#include "MassCommonFragments.h"
#include "MassNavigationFragments.h"
#include "MassStateTreeExecutionContext.h"
#include "NavigationSystem.h"
#include "StateTreeLinker.h"
#include "MassDebugger.h"
#include "NavFilters/NavigationQueryFilter.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanMassNavMeshFindPerpendicularFurthestPointTask)

bool FMetaHumanMassNavMeshFindPerpendicularFurthestPointTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(TransformHandle);
	Linker.LinkExternalData(AgentRadiusHandle);
	Linker.LinkExternalData(AgentHeightHandle);

	return true;
}

EStateTreeRunStatus FMetaHumanMassNavMeshFindPerpendicularFurthestPointTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	const FAgentRadiusFragment& AgentRadius = Context.GetExternalData(AgentRadiusHandle);
	const FAgentHeightFragment& AgentHeight = Context.GetExternalData(AgentHeightHandle);
	const FVector AgentLocation = Context.GetExternalData(TransformHandle).GetTransform().GetLocation();
	const FNavAgentProperties NavAgentProperties(AgentRadius.Radius, AgentHeight.Height);

	UNavigationSystemV1* NavSys = Cast<UNavigationSystemV1>(Context.GetWorld()->GetNavigationSystem());
	if (NavSys == nullptr)
	{
		MASSBEHAVIOR_LOG(Warning, TEXT("Invalid NavigationSystem"));
		return EStateTreeRunStatus::Failed;
	}

	const ANavigationData* NavData = NavSys->GetNavDataForProps(NavAgentProperties, AgentLocation);
	if (NavData == nullptr)
	{
		MASSBEHAVIOR_LOG(Warning, TEXT("Invalid NavData"));
		return EStateTreeRunStatus::Failed;
	}

	// Flatten the reference direction to the XY plane and normalize.
	FVector RefDir = InstanceData.ReferenceDirection;
	RefDir.Z = 0.f;
	if (!RefDir.Normalize())
	{
		// Degenerate input (e.g. straight-up vector): fall back to forward.
		RefDir = FVector::ForwardVector;
	}

	// @TODO: Use massive extents for now. Profile this later.
	FVector Extents = FVector(InstanceData.MaxSearchDistance, InstanceData.MaxSearchDistance, InstanceData.MaxSearchDistance);

	// Raycast along a direction from the agent and return the furthest reachable point.
	auto RaycastNavMesh = [&](const FVector& Direction) -> FVector
	{
		const FVector EndPoint = AgentLocation + Direction * InstanceData.MaxSearchDistance;
		FSharedConstNavQueryFilter Filter = UNavigationQueryFilter::GetQueryFilter(*NavData, nullptr, InstanceData.FilterClass);
		FNavLocation ProjectedLocation;
		NavSys->ProjectPointToNavigation(EndPoint, ProjectedLocation, Extents, NavData,Filter);
		return ProjectedLocation.Location;
	};

	const FVector ResultPos = RaycastNavMesh(RefDir);
	const FVector ResultNeg = RaycastNavMesh(-RefDir);

	const float DistancePos = FVector::Dist(ResultPos, AgentLocation);
	const float DistanceNeg = FVector::Dist(ResultNeg, AgentLocation);

	const FVector FurthestPoint = DistancePos >= DistanceNeg ? ResultPos : ResultNeg;

	InstanceData.TargetLocation.EndOfPathPosition = FurthestPoint;
	InstanceData.TargetLocation.EndOfPathIntent = EMassMovementAction::Move;

#if WITH_MASSGAMEPLAY_DEBUG
	const FMassStateTreeExecutionContext& MassContext = static_cast<const FMassStateTreeExecutionContext&>(Context);
	if (UE::Mass::Debug::IsDebuggingEntity(MassContext.GetEntity()))
	{
		MASSBEHAVIOR_LOG(Log, TEXT("Found furthest perpendicular point at %s (dist: %.1f)"),
			*FurthestPoint.ToString(), FMath::Max(DistancePos, DistanceNeg));
	}
#endif // WITH_MASSGAMEPLAY_DEBUG

	return EStateTreeRunStatus::Running;
}
