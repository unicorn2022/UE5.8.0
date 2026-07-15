// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNotify.h"

#include "AnimNotify_ResetClothingSimulation.generated.h"

UCLASS(const, hidecategories=Object, collapsecategories, meta=(DisplayName="Reset Clothing Simulation"), MinimalAPI)
class UAnimNotify_ResetClothingSimulation : public UAnimNotify
{
	GENERATED_BODY()

public:

	ENGINE_API UAnimNotify_ResetClothingSimulation();

	// Begin UAnimNotify interface
	ENGINE_API virtual FString GetNotifyName_Implementation() const override;
	ENGINE_API virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	// End UAnimNotify interface

};
