// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanMassResetMoverOnActorAcquireProcessor.h"

#include "DefaultMovementSet/InstantMovementEffects/BasicInstantMovementEffects.h"
#include "GameFramework/Actor.h"
#include "Mass/EntityFragments.h"
#include "MassActorSubsystem.h"
#include "MassExecutionContext.h"
#include "MassMoverInputComponent.h"
#include "MassMovementFragments.h"
#include "MassRepresentationFragments.h"
#include "MassRepresentationTypes.h"
#include "MoverComponent.h"
#include "MoverSimulationTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanMassResetMoverOnActorAcquireProcessor)

UMetaHumanMassResetMoverOnActorAcquireProcessor::UMetaHumanMassResetMoverOnActorAcquireProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
	// Run in the standard Mass -> world propagation group, after the input
	// translator has populated UMassMoverInputComponent for steady-state
	// entities. We need to write to the actor's Mover components and queue an
	// instant movement effect, both of which require the game thread.
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::UpdateWorldFromMass;
	bRequiresGameThreadExecution = true;
}

void UMetaHumanMassResetMoverOnActorAcquireProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadOnly);
}

void UMetaHumanMassResetMoverOnActorAcquireProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& InContext)
	{
		const TConstArrayView<FMassActorFragment> ActorList = InContext.GetFragmentView<FMassActorFragment>();
		const TConstArrayView<FMassVelocityFragment> VelocityList = InContext.GetFragmentView<FMassVelocityFragment>();
		const TConstArrayView<FTransformFragment> TransformList = InContext.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FMassRepresentationFragment> RepresentationList = InContext.GetFragmentView<FMassRepresentationFragment>();

		for (FMassExecutionContext::FEntityIterator EntityIt = InContext.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			const FMassRepresentationFragment& Representation = RepresentationList[EntityIt];

			// Detect the transition into an actor representation. UMassRepresentationProcessor
			// updates PrevRepresentation := old CurrentRepresentation at the top of each
			// per-entity step, so PrevRep != CurRep is the one-frame edge signal.
			const bool bJustAcquiredActor =
				UE::Mass::Representation::IsValidActorRepresentation(Representation.CurrentRepresentation)
				&& Representation.PrevRepresentation != Representation.CurrentRepresentation;
			if (!bJustAcquiredActor)
			{
				continue;
			}

			const AActor* EntityActor = ActorList[EntityIt].Get();
			if (EntityActor == nullptr)
			{
				continue;
			}

			UMoverComponent* MoverComp = EntityActor->FindComponentByClass<UMoverComponent>();
			UMassMoverInputComponent* MoverInputComp = EntityActor->FindComponentByClass<UMassMoverInputComponent>();
			if (MoverComp == nullptr || MoverInputComp == nullptr)
			{
				continue;
			}

			const FVector EntityVelocity = VelocityList[EntityIt].Value;
			const FQuat EntityRotation = TransformList[EntityIt].GetTransform().GetRotation();

			// Refresh the input bridge so the next ProduceInput emits a fresh
			// FCharacterDefaultInputs; otherwise the actor pool means the next
			// InputCmd carries the previous occupant's intent.
			MoverInputComp->SetDesiredVelocity(EntityVelocity);
			MoverInputComp->SetDesiredRotation(EntityRotation);

			// FApplyVelocityEffect always writes OutputState.MovementMode from
			// ForceMovementMode, so pass the current mode through to preserve it.
			TSharedPtr<FApplyVelocityEffect> ResetEffect = MakeShared<FApplyVelocityEffect>();
			ResetEffect->VelocityToApply = EntityVelocity;
			ResetEffect->bAdditiveVelocity = false;
			ResetEffect->ForceMovementMode = MoverComp->GetSyncState().MovementMode;
			MoverComp->QueueInstantMovementEffect(ResetEffect);
		}
	});
}
