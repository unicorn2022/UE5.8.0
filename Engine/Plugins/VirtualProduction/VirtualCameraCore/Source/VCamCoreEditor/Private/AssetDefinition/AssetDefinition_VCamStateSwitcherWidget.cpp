// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_VCamStateSwitcherWidget.h"

#include "Factories/VCamStateSwitcherWidgetFactory.h"
#include "UI/Switcher/VCamStateSwitcherWidget.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_VCamStateSwitcherWidget"

FText UAssetDefinition_VCamStateSwitcherWidget::GetAssetDisplayName() const
{
	return LOCTEXT("VCamStateSwitcherWidget_AssetName", "VCam State Switcher Widget");
}

TSoftClassPtr<UObject> UAssetDefinition_VCamStateSwitcherWidget::GetAssetClass() const
{
	return UVCamStateSwitcherWidget::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_VCamStateSwitcherWidget::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] =
	{
		FAssetCategoryPath(FAssetCategoryPath(LOCTEXT("VCamStateSwitcherWidget_AssetCategory", "Virtual Production")), LOCTEXT("VCamStateSwitcherWidget_CategorySection", "Virtual Camera"), ECategoryMenuType::Section)
	};

	return Categories;
}

UFactory* UAssetDefinition_VCamStateSwitcherWidget::GetFactoryForBlueprintType(UBlueprint* InBlueprint) const
{
	UVCamStateSwitcherWidgetFactory* BlueprintFactory = NewObject<UVCamStateSwitcherWidgetFactory>();
	BlueprintFactory->ParentClass = TSubclassOf<UVCamStateSwitcherWidget>(*InBlueprint->GeneratedClass);
	return BlueprintFactory;
}

#undef LOCTEXT_NAMESPACE
