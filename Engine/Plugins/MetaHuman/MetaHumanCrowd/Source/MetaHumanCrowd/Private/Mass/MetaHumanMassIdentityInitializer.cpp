// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanMassIdentityInitializer.h"

#include "Mass/MetaHumanCrowdAppearanceProvider.h"
#include "Mass/MetaHumanMassFragments.h"
#include "Mass/MetaHumanMassRepresentationSubsystem.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassRepresentationFragments.h"
#include "Logging/StructuredLog.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanMassIdentityInitializer)

//----------------------------------------------------------------------//
//  UMetaHumanMassIdentityInitializer
//----------------------------------------------------------------------//
UMetaHumanMassIdentityInitializer::UMetaHumanMassIdentityInitializer()
	: EntityQuery(*this)
{
	ObservedTypes.Add(FMassRepresentationFragment::StaticStruct());
	ObservedOperations = EMassObservedOperationFlags::Add;
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;
}

void UMetaHumanMassIdentityInitializer::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FMetaHumanMassIdentityFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery.AddSharedRequirement<FMetaHumanAppearanceSharedFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddSubsystemRequirement<UMetaHumanMassRepresentationSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UMetaHumanMassIdentityInitializer::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, [this](FMassExecutionContext& Context)
	{
		UMetaHumanMassRepresentationSubsystem& MHRepresentationSubsystem = Context.GetMutableSubsystemChecked<UMetaHumanMassRepresentationSubsystem>();
		FMetaHumanAppearanceSharedFragment& AppearanceFragment = Context.GetMutableSharedFragment<FMetaHumanAppearanceSharedFragment>();
		const TArrayView<FMassRepresentationFragment> RepresentationList = Context.GetMutableFragmentView<FMassRepresentationFragment>();
		const TArrayView<FMetaHumanMassIdentityFragment> IdentityFragmentList = Context.GetMutableFragmentView<FMetaHumanMassIdentityFragment>();
		const TConstArrayView<FTransformFragment> TransformList = Context.GetFragmentView<FTransformFragment>();

		// Resolve the procedural provider once per chunk. The provider is owned by the subsystem
		// and stable for (Class, World), so we don't store a pointer in the shared fragment.
		UMetaHumanCrowdAppearanceProvider* Provider = nullptr;
		if (AppearanceFragment.ProviderClass)
		{
			Provider = MHRepresentationSubsystem.GetOrCreateProvider(AppearanceFragment.ProviderClass);
			if (Provider == nullptr)
			{
				UE_LOGFMT(LogMetaHumanMassRepresentation, Warning,
					"IdentityInitializer: ProviderClass {Class} is set on the trait but GetOrCreateProvider returned null.",
					AppearanceFragment.ProviderClass->GetName());
			}
		}

		const bool bHasArrayPool = !AppearanceFragment.AssignedAppearanceIndices.IsEmpty();

		if (Provider == nullptr && !bHasArrayPool)
		{
			UE_LOGFMT(LogMetaHumanMassRepresentation, Warning,
				"IdentityInitializer: chunk has neither a procedural Provider nor a pre-baked CharacterInstances pool. "
				"Entities will have invalid SkinnedMeshDescHandle and likely trip downstream ensures.");
		}

		for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			uint32 AppearanceId = FMetaHumanMassIdentityFragment::InvalidAppearanceIndex;

			if (Provider != nullptr)
			{
				const FVector SpawnLocation = TransformList.IsEmpty()
					? FVector::ZeroVector
					: TransformList[EntityIt].GetTransform().GetLocation();
				const FMetaHumanCrowdAppearanceHandle Handle = Provider->AcquireAppearance(&MHRepresentationSubsystem, SpawnLocation);
				if (MHRepresentationSubsystem.IsValidAppearanceHandle(Handle))
				{
					AppearanceId = Handle.GetIndex();
				}
				else
				{
					UE_LOGFMT(LogMetaHumanMassRepresentation, Warning,
						"IdentityInitializer: Provider {Class} returned invalid handle (raw={Raw}) for spawn at {Location}. "
						"Common cause: MHI assembly failed. Check for assembly failure messages in the log.",
						Provider->GetClass()->GetName(),
						Handle.GetIndex(),
						SpawnLocation.ToString());
				}
			}

			if (AppearanceId == FMetaHumanMassIdentityFragment::InvalidAppearanceIndex && bHasArrayPool)
			{
				AppearanceFragment.CurrentIndex = (AppearanceFragment.CurrentIndex + 1) % AppearanceFragment.AssignedAppearanceIndices.Num();
				AppearanceId = AppearanceFragment.AssignedAppearanceIndices[AppearanceFragment.CurrentIndex];
			}

			if (AppearanceId == FMetaHumanMassIdentityFragment::InvalidAppearanceIndex)
			{
				// Leaving SkinnedMeshDescHandle at its default (invalid) will trip ensures in
				// MassStationaryISMSwitcherProcessor / MassConsumeInstancedSkinnedMeshAnimationProcessor
				// once the entity transitions to skinned-mesh-instance LOD. Logged so the cause is obvious.
				UE_LOGFMT(LogMetaHumanMassRepresentation, Warning,
					"IdentityInitializer: no AppearanceId chosen for an entity. SkinnedMeshDescHandle will remain invalid.");
				continue;
			}

			MHRepresentationSubsystem.OnEntityAssignedAppearance(AppearanceId);

			IdentityFragmentList[EntityIt].AppearanceIndex = AppearanceId;
			RepresentationList[EntityIt].SkinnedMeshDescHandle = MHRepresentationSubsystem.GetSkinnedMeshInstanceByAppearanceId(AppearanceId);
		}
	});
}
