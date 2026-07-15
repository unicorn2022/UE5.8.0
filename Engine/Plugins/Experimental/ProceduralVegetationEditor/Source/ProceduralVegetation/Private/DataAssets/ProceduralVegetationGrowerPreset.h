// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Implementations/PVGrower.h"
#include "ProceduralVegetationGrowerPreset.generated.h"

UCLASS(BlueprintType)
class PROCEDURALVEGETATION_API UProceduralVegetationGrowerPreset : public UDataAsset
{
	GENERATED_BODY()
public:
	virtual void PostLoad() override;

	UPROPERTY(EditAnywhere, Category = Settings, meta=( ShowOnlyInnerProperties))
	FPVGrowerParams GrowthParams;
};
