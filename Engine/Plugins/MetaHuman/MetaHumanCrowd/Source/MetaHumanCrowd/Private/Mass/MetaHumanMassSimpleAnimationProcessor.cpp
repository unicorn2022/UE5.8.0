// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanMassSimpleAnimationProcessor.h"

#include "Mass/MetaHumanMassRepresentationSubsystem.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "MassRepresentationFragments.h"
#include "MassLODFragments.h"
#include "MassVisualizationComponent.h"
#include "MassRepresentationTypes.h"
#include "MassSkinnedMeshRepresentationTypes.h"
#include "Mass/MetaHumanMassFragments.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanMassSimpleAnimationProcessor)

UMetaHumanMassSimpleAnimationProcessor::UMetaHumanMassSimpleAnimationProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);

	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
	bRequiresGameThreadExecution = false;
}

void UMetaHumanMassSimpleAnimationProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FMassRepresentationAnimationFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSubsystemRequirement<UMetaHumanMassRepresentationSubsystem>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSharedRequirement<FMetaHumanMassAnimationScalabilitySharedFragment>(EMassFragmentAccess::ReadOnly);
}

void UMetaHumanMassSimpleAnimationProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, [this](FMassExecutionContext& Context)
	{
		const FMetaHumanMassAnimationScalabilitySharedFragment& ScalabilityConfig = Context.GetSharedFragment<FMetaHumanMassAnimationScalabilitySharedFragment>();
		const TConstArrayView<FMassVelocityFragment> VelocitiesList = Context.GetFragmentView<FMassVelocityFragment>();
		const TConstArrayView<FMassRepresentationFragment> RepresentationList = Context.GetFragmentView<FMassRepresentationFragment>();
		const TArrayView<FMassRepresentationAnimationFragment> AnimDataList = Context.GetMutableFragmentView<FMassRepresentationAnimationFragment>();

		const UMetaHumanMassRepresentationSubsystem& RepresentationSubsystem = Context.GetSubsystemChecked<UMetaHumanMassRepresentationSubsystem>();
		for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			const FVector& CurrentVelocity = VelocitiesList[EntityIt].Value;
			FAnimSequenceTrackAutoPlayData& AnimData = AnimDataList[EntityIt].AnimData;

			const FMassRepresentationFragment& Representation = RepresentationList[EntityIt];
			if (!UE::Mass::Representation::IsValidActorRepresentation(Representation.CurrentRepresentation))
			{
				// Simple animation update: 0 = idle, 1 = walk. Update with slight hysteresis.
				// Clamp to the valid range first -- an actor-representation path (UAF) may have
				// written an arbitrary sequence index that would produce a negative result here.
				AnimData.SequenceIndex = FMath::Clamp(AnimData.SequenceIndex, 0, 1);
				const double Speed = CurrentVelocity.Size();
				if ((AnimData.SequenceIndex == 0 && Speed > 100) || (AnimData.SequenceIndex == 1 && Speed < 50))
				{
					AnimData.SequenceIndex = 1 - AnimData.SequenceIndex;
					AnimData.BlendTime = ScalabilityConfig.AnimBlendTime;
				}
			}
		}
	});
}
