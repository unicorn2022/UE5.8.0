// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassMoverInputComponent.h"
#include "GameFramework/Actor.h"

void UMassMoverInputComponent::BeginPlay()
{
	Super::BeginPlay();

	// Sync to initial actor state
	DesiredRotation = GetOwner()->GetActorRotation().Quaternion();
	DesiredVelocity = FVector::Zero();
}

void UMassMoverInputComponent::ProduceInput_Implementation(int32 DeltaMs, FMoverInputCmdContext& OutInputCmd)
{
	FCharacterDefaultInputs& CharacterInputs = OutInputCmd.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>();

	CharacterInputs.SetMoveInput(EMoveInputType::Velocity, DesiredVelocity);
	CharacterInputs.OrientationIntent = DesiredRotation.GetForwardVector();
}
