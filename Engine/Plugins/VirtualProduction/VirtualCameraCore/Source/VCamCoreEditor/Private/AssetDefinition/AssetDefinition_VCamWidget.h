// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Script/AssetDefinition_Blueprint.h"
#include "AssetDefinition_VCamWidget.generated.h"

UCLASS()
class UAssetDefinition_VCamWidget : public UAssetDefinition_Blueprint
{
	GENERATED_BODY()

public:
	// UAssetDefinition Begin
	virtual FText GetAssetDisplayName() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
	virtual UFactory* GetFactoryForBlueprintType(UBlueprint* InBlueprint) const override;
	// UAssetDefinition End
};
