// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanMassIdentityDeinitializer.h"

#include "Mass/MetaHumanCrowdAppearanceProvider.h"
#include "Mass/MetaHumanMassFragments.h"
#include "Mass/MetaHumanMassRepresentationSubsystem.h"
#include "MassExecutionContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanMassIdentityDeinitializer)

UMetaHumanMassIdentityDeinitializer::UMetaHumanMassIdentityDeinitializer()
	: EntityQuery(*this)
{
	ObservedTypes.Add(FMetaHumanMassIdentityFragment::StaticStruct());
	ObservedOperations = EMassObservedOperationFlags::Remove;
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;
	bRequiresGameThreadExecution = true;
}

void UMetaHumanMassIdentityDeinitializer::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FMetaHumanMassIdentityFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::All);
	EntityQuery.AddSharedRequirement<FMetaHumanAppearanceSharedFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSubsystemRequirement<UMetaHumanMassRepresentationSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UMetaHumanMassIdentityDeinitializer::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, [this](FMassExecutionContext& Context)
	{
		UMetaHumanMassRepresentationSubsystem& MHRepresentationSubsystem = Context.GetMutableSubsystemChecked<UMetaHumanMassRepresentationSubsystem>();
		const FMetaHumanAppearanceSharedFragment& AppearanceFragment = Context.GetSharedFragment<FMetaHumanAppearanceSharedFragment>();
		const TConstArrayView<FMetaHumanMassIdentityFragment> IdentityFragmentList = Context.GetFragmentView<FMetaHumanMassIdentityFragment>();

		UMetaHumanCrowdAppearanceProvider* Provider = nullptr;
		if (AppearanceFragment.ProviderClass)
		{
			Provider = MHRepresentationSubsystem.GetExistingProvider(AppearanceFragment.ProviderClass);
		}

		for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			const uint32 AppearanceId = IdentityFragmentList[EntityIt].AppearanceIndex;
			if (AppearanceId == FMetaHumanMassIdentityFragment::InvalidAppearanceIndex)
			{
				continue;
			}

			if (Provider != nullptr)
			{
				Provider->OnEntityDespawned(&MHRepresentationSubsystem, FMetaHumanCrowdAppearanceHandle(AppearanceId));
			}

			// Decrement the refcount last; if the provider just unregistered, this is the call
			// that lets the registry physically release the entry.
			MHRepresentationSubsystem.OnEntityReleasedAppearance(AppearanceId);
		}
	});
}
