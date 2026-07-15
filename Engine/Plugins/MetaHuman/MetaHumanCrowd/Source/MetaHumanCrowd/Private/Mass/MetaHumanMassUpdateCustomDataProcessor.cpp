// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanMassUpdateCustomDataProcessor.h"

#include "Mass/MetaHumanMassFragments.h"
#include "Mass/MetaHumanMassRepresentationSubsystem.h"

#include "MassCommonFragments.h"
#include "MassEntityManager.h"
#include "MassExecutionContext.h"
#include "MassLODFragments.h"
#include "MassRepresentationFragments.h"
#include "MassSkinnedMeshRepresentationTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanMassUpdateCustomDataProcessor)

UMetaHumanMassUpdateCustomDataProcessor::UMetaHumanMassUpdateCustomDataProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);

	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Representation;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::VisualizationProcessing);
	// ExecuteBefore so we read correct PrevLODSig.
	ExecutionOrder.ExecuteBefore.Add(TEXT("MassUpdateInstancedSkinnedMeshProcessor"));
	bRequiresGameThreadExecution = true;
}

void UMetaHumanMassUpdateCustomDataProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	// Filter must match UMassUpdateInstancedSkinnedMeshProcessor's so that for every entity that
	// processor pushes to a Range's EntitiesRequiringUpdate, this processor appends matching floats --
	// EndVisualChanges divides MeshInstanceCustomFloats.Num() by EntitiesRequiringUpdate.Num().
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassRepresentationLODFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMetaHumanMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddChunkRequirement<FMassVisualizationChunkFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.SetChunkFilter(&FMassVisualizationChunkFragment::AreAnyEntitiesVisibleInChunk);
	// ReadWrite: the per-Range AddBatched* path calls GetAndMarkDirty on the shared mesh-info map.
	EntityQuery.AddSubsystemRequirement<UMetaHumanMassRepresentationSubsystem>(EMassFragmentAccess::ReadWrite);

	EntityQuery.AddTagRequirement<FMassStaticRepresentationTag>(EMassFragmentPresence::None);
}

void UMetaHumanMassUpdateCustomDataProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Context)
	{
		UMetaHumanMassRepresentationSubsystem& RepresentationSubsystem = Context.GetMutableSubsystemChecked<UMetaHumanMassRepresentationSubsystem>();
		FMassInstancedSkinnedMeshInfoArrayView SkinnedMeshInfos = RepresentationSubsystem.GetMutableInstancedSkinnedMeshInfos();

		const TConstArrayView<FMassRepresentationFragment> RepresentationList = Context.GetFragmentView<FMassRepresentationFragment>();
		const TConstArrayView<FMassRepresentationLODFragment> RepresentationLODList = Context.GetFragmentView<FMassRepresentationLODFragment>();
		const TConstArrayView<FMetaHumanMassIdentityFragment> IdentityList = Context.GetFragmentView<FMetaHumanMassIdentityFragment>();

		for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			const FMassRepresentationFragment& Representation = RepresentationList[EntityIt];
			if (Representation.CurrentRepresentation != EMassRepresentationType::SkinnedMeshInstance)
			{
				continue;
			}

			const uint32 AppearanceId = IdentityList[EntityIt].AppearanceIndex;
			if (AppearanceId == FMetaHumanMassIdentityFragment::InvalidAppearanceIndex)
			{
				continue;
			}

			const int32 SkinnedMeshInfoIndex = Representation.SkinnedMeshDescHandle.ToIndex();
			if (!SkinnedMeshInfos.IsValidIndex(SkinnedMeshInfoIndex))
			{
				continue;
			}

			const TArray<TArray<float>>& PerMeshFloats =
				RepresentationSubsystem.GetCustomDataFloatsPerMesh(AppearanceId);
			if (!PerMeshFloats.IsEmpty())
			{
				SkinnedMeshInfos[SkinnedMeshInfoIndex].AddBatchedCustomDataFloats(
					PerMeshFloats,
					RepresentationLODList[EntityIt].LODSignificance,
					Representation.PrevLODSignificance);
			}
		}
	});
}
