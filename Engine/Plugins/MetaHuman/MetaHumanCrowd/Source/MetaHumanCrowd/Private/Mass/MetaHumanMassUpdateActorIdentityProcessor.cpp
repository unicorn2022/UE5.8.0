// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanMassUpdateActorIdentityProcessor.h"

#include "MassActorSubsystem.h"
#include "MassExecutionContext.h"
#include "MetaHumanCharacterActorInterface.h"
#include "Mass/MetaHumanMassFragments.h"
#include "Mass/MetaHumanMassRepresentationSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanMassUpdateActorIdentityProcessor)

UMetaHumanMassUpdateActorIdentityProcessor::UMetaHumanMassUpdateActorIdentityProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);

	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Representation);
	bRequiresGameThreadExecution = true;
}

void UMetaHumanMassUpdateActorIdentityProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMetaHumanMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSubsystemRequirement<UMetaHumanMassRepresentationSubsystem>(EMassFragmentAccess::ReadOnly);
}

void UMetaHumanMassUpdateActorIdentityProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{

	EntityQuery.ForEachEntityChunk(Context, [this](FMassExecutionContext& Context)
	{
		const UMetaHumanMassRepresentationSubsystem& MHRepresentationSubsystem = Context.GetSubsystemChecked<UMetaHumanMassRepresentationSubsystem>();
		const TArrayView<const FMetaHumanMassIdentityFragment> IdentityFragmentList = Context.GetFragmentView<FMetaHumanMassIdentityFragment>();
		const TArrayView<FMassActorFragment> ActorFragmentList = Context.GetMutableFragmentView<FMassActorFragment>();

		for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			AActor* EntityActor = ActorFragmentList[EntityIt].GetMutable();
			if (EntityActor && EntityActor->Implements<UMetaHumanCharacterActorInterface>())
			{
				// AppearanceIndex is InvalidAppearanceIndex until UMetaHumanMassIdentityInitializer has run for this entity.
				const uint32 MHIIndex = IdentityFragmentList[EntityIt].AppearanceIndex;
				if (MHIIndex != FMetaHumanMassIdentityFragment::InvalidAppearanceIndex)
				{
					if (UMetaHumanInstance* Instance = MHRepresentationSubsystem.GetMetaHumanInstanceByAppearanceId(MHIIndex))
					{
						if (IMetaHumanCharacterActorInterface::Execute_GetMetaHumanInstance(EntityActor) != Instance)
						{
							IMetaHumanCharacterActorInterface::Execute_SetMetaHumanInstance(EntityActor, Instance);
						}
					}
				}
			}
		}
	});
}
