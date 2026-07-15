// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_EditorOnlyModifier.h"

#include "EditorOnlyVCamModifier.h"
#include "EditorOnlyVCamModifierBlueprint.h"
#include "Factories/EditorOnlyModifierFactory.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_EditorOnlyModifier"

FText UAssetDefinition_EditorOnlyModifier::GetAssetDisplayName() const
{
	return LOCTEXT("EditorOnlyModifier_AssetName", "VCam Editor Only Modifier");
}

TSoftClassPtr<UObject> UAssetDefinition_EditorOnlyModifier::GetAssetClass() const
{
	return UEditorOnlyVCamModifierBlueprint::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_EditorOnlyModifier::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] =
	{
		FAssetCategoryPath(FAssetCategoryPath(LOCTEXT("EditorOnlyModifier_AssetCategory", "Virtual Production")), LOCTEXT("EditorOnlyModifier_CategorySection", "Virtual Camera"), ECategoryMenuType::Section)
	};

	return Categories;
}

UFactory* UAssetDefinition_EditorOnlyModifier::GetFactoryForBlueprintType(UBlueprint* InBlueprint) const
{
	UEditorOnlyModifierFactory* BlueprintFactory = NewObject<UEditorOnlyModifierFactory>();
	BlueprintFactory->ParentClass = TSubclassOf<UEditorOnlyVCamModifier>(*InBlueprint->GeneratedClass);
	return BlueprintFactory;
}

#undef LOCTEXT_NAMESPACE
