// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_VCamWidget.h"

#include "Factories/VCamWidgetFactory.h"
#include "UI/VCamWidget.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_VCamWidget"

FText UAssetDefinition_VCamWidget::GetAssetDisplayName() const
{
	return LOCTEXT("VCamWidget_AssetName", "VCam Widget");
}

TSoftClassPtr<UObject> UAssetDefinition_VCamWidget::GetAssetClass() const
{
	return UVCamWidget::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_VCamWidget::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] =
	{
		FAssetCategoryPath(FAssetCategoryPath(LOCTEXT("VCamWidget_AssetCategory", "Virtual Production")), LOCTEXT("VCamWidget_CategorySection", "Virtual Camera"), ECategoryMenuType::Section)
	};

	return Categories;
}

UFactory* UAssetDefinition_VCamWidget::GetFactoryForBlueprintType(UBlueprint* InBlueprint) const
{
	UVCamWidgetFactory* BlueprintFactory = NewObject<UVCamWidgetFactory>();
	BlueprintFactory->ParentClass = TSubclassOf<UVCamWidget>(*InBlueprint->GeneratedClass);
	return BlueprintFactory;
}

#undef LOCTEXT_NAMESPACE
