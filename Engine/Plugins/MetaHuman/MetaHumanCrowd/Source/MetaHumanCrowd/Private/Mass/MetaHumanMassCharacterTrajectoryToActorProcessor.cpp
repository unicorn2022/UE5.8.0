// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanMassCharacterTrajectoryToActorProcessor.h"

#include "MassActorSubsystem.h"
#include "MassExecutionContext.h"
#include "MassRepresentationFragments.h"
#include "MassVisualizationComponent.h"
#include "Mass/IMetahumanMassCrowdActorBlueprintInterface.h"
#include "Mass/MetaHumanMassCrowdTags.h"
#include "MassCharacterTrajectoryFragments.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanMassCharacterTrajectoryToActorProcessor)

UMetaHumanMassCharacterTrajectoryToActorProcessor::UMetaHumanMassCharacterTrajectoryToActorProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);

	// Blueprints are not thread-safe so run this on game-thread for safety.
	// This is safe to change for games that can guarantee this processor will not update in parallel with the animation update.
	bRequiresGameThreadExecution = true;
}

void UMetaHumanMassCharacterTrajectoryToActorProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FCharacterTrajectoryFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FMetahumanMassCrowdActorTag>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadOnly);
}

void UMetaHumanMassCharacterTrajectoryToActorProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, [this](FMassExecutionContext& Context)
	{
		const TConstArrayView<FCharacterTrajectoryFragment> TrajectoryList = Context.GetFragmentView<FCharacterTrajectoryFragment>();
		const TConstArrayView<FMassRepresentationFragment> RepresentationList = Context.GetFragmentView<FMassRepresentationFragment>();

		const TArrayView<FMassActorFragment> ActorList = Context.GetMutableFragmentView<FMassActorFragment>();

		for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			const FMassRepresentationFragment& Representation = RepresentationList[EntityIt];
			if (UE::Mass::Representation::IsValidActorRepresentation(Representation.CurrentRepresentation))
			{
				if (AActor* EntityActor = ActorList[EntityIt].GetMutable())
				{
					IMetahumanMassCrowdActorBlueprintInterface::Execute_SetTrajectory(EntityActor, TrajectoryList[EntityIt].Trajectory);
				}
			}
		}
	});
}
