// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_VCamModifier.h"

#include "Factories/VCamModifierFactory.h"
#include "Modifier/VCamModifier.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_VCamModifier"

FText UAssetDefinition_VCamModifier::GetAssetDisplayName() const
{
	return LOCTEXT("VCamModifier_AssetName", "VCam Modifier");
}

TSoftClassPtr<UObject> UAssetDefinition_VCamModifier::GetAssetClass() const
{
	return UVCamBlueprintModifier::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_VCamModifier::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] =
	{
		FAssetCategoryPath(FAssetCategoryPath(LOCTEXT("VCamModifier_AssetCategory", "Virtual Production")), LOCTEXT("VCamModifier_CategorySection", "Virtual Camera"), ECategoryMenuType::Section)
	};

	return Categories;
}

UFactory* UAssetDefinition_VCamModifier::GetFactoryForBlueprintType(UBlueprint* InBlueprint) const
{
	UVCamModifierFactory* BlueprintFactory = NewObject<UVCamModifierFactory>();
	BlueprintFactory->ParentClass = TSubclassOf<UVCamBlueprintModifier>(*InBlueprint->GeneratedClass);
	return BlueprintFactory;
}

#undef LOCTEXT_NAMESPACE
