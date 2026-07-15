// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanMassActorAnimationProcessor.h"

#include "Mass/MetaHumanMassRepresentationSubsystem.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "MassRepresentationFragments.h"
#include "MassLODFragments.h"
#include "MassVisualizationComponent.h"
#include "MassRepresentationTypes.h"
#include "MassSkinnedMeshRepresentationTypes.h"
#include "Mass/MetaHumanMassFragments.h"
#include "Animation/AnimSequence.h"
#include "Mass/IMetahumanMassCrowdActorBlueprintInterface.h"
#include "Mass/MetaHumanMassAnimDesc.h"
#include "Mass/MetaHumanMassCrowdTags.h"
#include "MassActorSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanMassActorAnimationProcessor)

UMetaHumanMassActorProcessor::UMetaHumanMassActorProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);

	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);

	// Blueprints are not thread-safe so run this on game-thread for safety.
	// This is safe to change for games that can guarantee this processor will not update in parallel with the animation update.
	bRequiresGameThreadExecution = true;
}

void UMetaHumanMassActorProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FMassRepresentationAnimationFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
	EntityQuery.AddTagRequirement<FMetahumanMassCrowdActorTag>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassRepresentationLODFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSubsystemRequirement<UMetaHumanMassRepresentationSubsystem>(EMassFragmentAccess::ReadOnly);
}

void UMetaHumanMassActorProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, [this](FMassExecutionContext& Context)
	{
		const TConstArrayView<FMassRepresentationFragment> RepresentationList = Context.GetFragmentView<FMassRepresentationFragment>();
		const TArrayView<FMassRepresentationAnimationFragment> AnimDataList = Context.GetMutableFragmentView<FMassRepresentationAnimationFragment>();
		const TConstArrayView<FMassRepresentationLODFragment> RepresentationLODList = Context.GetFragmentView<FMassRepresentationLODFragment>();

		const TArrayView<FMassActorFragment> ActorList = Context.GetMutableFragmentView<FMassActorFragment>();
		const bool bUsesActors = ActorList.Num() > 0;

		if (bUsesActors)
		{
			const UMetaHumanMassRepresentationSubsystem& RepresentationSubsystem = Context.GetSubsystemChecked<UMetaHumanMassRepresentationSubsystem>();
			for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
			{
				const FMassRepresentationFragment& Representation = RepresentationList[EntityIt];
				if (UE::Mass::Representation::IsValidActorRepresentation(Representation.CurrentRepresentation))
				{
					if (AActor* EntityActor = ActorList[EntityIt].GetMutable())
					{
						FAnimSequenceTrackAutoPlayData& AnimData = AnimDataList[EntityIt].AnimData;
						const FMassRepresentationLODFragment& RepresentationLOD = RepresentationLODList[EntityIt];

						FMetahumanMassAnimDesc MassAnimDesc;
						// First frame of the ISKM->Actor swap. Initialize Actor state with whatever ISKM is using.
						if (Representation.CurrentRepresentation != Representation.PrevRepresentation)
						{
							// Look up resolved animation state from the visualization component.
							// Populated during EndVisualChanges() where ISKM track data is already hot in cache.
							if (UMassVisualizationComponent* VizComp = RepresentationSubsystem.GetVisualizationComponent())
							{
								if (const FMassSkinnedMeshResolvedAnimState* State = VizComp->FindResolvedAnimState(Context.GetEntity(EntityIt)))
								{
									MassAnimDesc.AnimSequence = State->AnimSequence.Get();
									MassAnimDesc.AnimSequenceIndex = State->SequenceIndex;
									MassAnimDesc.Position = State->Position;
								}
							}

							// We purposely set this to true only once. It's up to the Actor to say it's "done"
							MassAnimDesc.bJustSwapped = true;
						}
						else
						{
							// Subsequent frames of Actor update. Need to populate ISKM fragment data with the current state of the Actor.
							MassAnimDesc = IMetahumanMassCrowdActorBlueprintInterface::Execute_GetMetaHumanMassAnimDesc(EntityActor);
							AnimData.BlendTime = 0.0f;
						}

						// We always need to update significance since this will drive the "simple graph" alpha.
						MassAnimDesc.Significance = RepresentationLOD.LODSignificance;
						IMetahumanMassCrowdActorBlueprintInterface::Execute_SetMetaHumanMassAnimDesc(EntityActor, MassAnimDesc);
					}
				}
			}
		}
	});
}
