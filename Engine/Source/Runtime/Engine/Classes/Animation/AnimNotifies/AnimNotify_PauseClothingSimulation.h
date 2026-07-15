// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNotify.h"

#include "AnimNotify_PauseClothingSimulation.generated.h"

UCLASS(const, hidecategories=Object, collapsecategories, meta=(DisplayName="Pause Clothing Simulation"), MinimalAPI)
class UAnimNotify_PauseClothingSimulation : public UAnimNotify
{
	GENERATED_BODY()

public:

	ENGINE_API UAnimNotify_PauseClothingSimulation();

	// Begin UAnimNotify interface
	ENGINE_API virtual FString GetNotifyName_Implementation() const override;
	
	ENGINE_API virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	// End UAnimNotify interface

};
