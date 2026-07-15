// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCrowdStatsLibrary.h"

#include "Engine/World.h"
#include "MassCrowdFragments.h"
#include "MassEntityManager.h"
#include "MassEntityQuery.h"
#include "MassEntityTypes.h"
#include "MassEntityUtils.h"
#include "MassExecutionContext.h"
#include "MassLODFragments.h"
#include "MassLODTypes.h"
#include "MassRepresentationFragments.h"
#include "MassRepresentationTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanCrowdStatsLibrary)


FMetaHumanCrowdStats UMetaHumanCrowdStatsLibrary::GatherMetaHumanCrowdStats(const UObject* WorldContextObject)
{
	FMetaHumanCrowdStats Stats;

	if (WorldContextObject == nullptr)
	{
		return Stats;
	}

	const UWorld* World = WorldContextObject->GetWorld();
	if (World == nullptr)
	{
		return Stats;
	}

	FMassEntityManager* CrowdEntityManager = UE::Mass::Utils::GetEntityManager(World);
	if (CrowdEntityManager == nullptr)
	{
		return Stats;
	}

	// NOTE: This performs an ad-hoc Mass entity query outside the processor pipeline.
	//
	// It's safe here because we run on the game thread between processing phases, with read-only
	// access.
	//
	// Do NOT call from within a Mass processor or from a background thread -- the query bypasses
	// Mass's scheduling guarantees and would race with concurrent chunk iteration.
	FMassEntityQuery Query(CrowdEntityManager->AsShared());
	Query.AddTagRequirement<FMassCrowdTag>(EMassFragmentPresence::All);
	Query.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FMassRepresentationLODFragment>(EMassFragmentAccess::ReadOnly);

	FMassExecutionContext ExecutionContext(*CrowdEntityManager);
	Query.ForEachEntityChunk(ExecutionContext, [&Stats](FMassExecutionContext& Context)
	{
		const TConstArrayView<FMassRepresentationFragment> RepresentationList = Context.GetFragmentView<FMassRepresentationFragment>();
		const TConstArrayView<FMassRepresentationLODFragment> RepresentationLODList = Context.GetFragmentView<FMassRepresentationLODFragment>();

		for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			++Stats.NumEntities;

			const FMassRepresentationLODFragment& RepresentationLOD = RepresentationLODList[EntityIt];
			if (RepresentationLOD.Visibility == EMassVisibility::CanBeSeen)
			{
				++Stats.NumVisible;
			}

			const FMassRepresentationFragment& Representation = RepresentationList[EntityIt];
			if (UE::Mass::Representation::IsValidActorRepresentation(Representation.CurrentRepresentation))
			{
				++Stats.NumActors;
			}
			else if (Representation.CurrentRepresentation == EMassRepresentationType::SkinnedMeshInstance)
			{
				++Stats.NumISKMs;
			}
		}
	});

	return Stats;
}
