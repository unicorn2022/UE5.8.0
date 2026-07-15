// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetaHumanMassCrowdRepresentationActorManagement.h"

#include "Mass/MetaHumanMassRepresentationSubsystem.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanMassCrowdRepresentationActorManagement)

void UMetaHumanMassCrowdRepresentationActorManagement::SetActorEnabled(const EMassActorEnabledType EnabledType, AActor& Actor, const int32 EntityIdx, FMassCommandBuffer& CommandBuffer) const
{
	Super::SetActorEnabled(EnabledType, Actor, EntityIdx, CommandBuffer);

	// Visibility calculation is not always correct for MHs so we need to fix up.
	const bool bEnabled = EnabledType != EMassActorEnabledType::Disabled;

	TInlineComponentArray<UActorComponent*> ActorComponents;
	Actor.GetComponents(ActorComponents);

	static const FName HiddenTag("Hidden");
	
	for (UActorComponent* Component : ActorComponents)
	{
		Component->RegisterAllComponentTickFunctions(bEnabled && Component->PrimaryComponentTick.bStartWithTickEnabled);

		if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
		{
			SceneComponent->SetVisibility(bEnabled && !SceneComponent->ComponentTags.Contains(HiddenTag));
		}
	}
}
