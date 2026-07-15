// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneStateBlueprint.h"
#include "AvaSceneStateBlueprint.generated.h"

#define UE_API AVALANCHESCENESTATEBLUEPRINT_API

UCLASS(MinimalAPI, DisplayName="Motion Design Scene State Blueprint")
class UAvaSceneStateBlueprint : public USceneStateBlueprint
{
	GENERATED_BODY()

public:
	//~ Begin UObject
	UE_API virtual void PostLoad() override;
	//~ End UObject
};

#undef UE_API
