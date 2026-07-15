// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanMassTargetLocationEvaluator.h"

#include "MassStateTreeExecutionContext.h"
#include "Mass/MetaHumanMassFragments.h"

void FMetaHumanMassTargetLocationEvaluator::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
	FMetaHumanMassTargetReaderInstanceData& InstanceData = Context.GetInstanceData<FMetaHumanMassTargetReaderInstanceData>(*this);

	const FMetaHumanMassTargetLocationFragment* CommandedTarget =
		MassContext.GetEntityManager().GetFragmentDataPtr<FMetaHumanMassTargetLocationFragment>(MassContext.GetEntity());

	if (CommandedTarget)
	{
		InstanceData.TargetLocation.EndOfPathPosition = CommandedTarget->TargetLocation;
		InstanceData.TargetLocation.EndOfPathIntent = EMassMovementAction::Move;
	}
}