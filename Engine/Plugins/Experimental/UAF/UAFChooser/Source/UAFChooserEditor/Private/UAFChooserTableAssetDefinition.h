// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UAFAnimChooser.h"
#include "AssetDefinitionDefault.h"
#include "UAFChooserTableAssetDefinition.generated.h"

#define LOCTEXT_NAMESPACE "AnimNextAssetDefinitions"

UCLASS()
class UAssetDefinition_UAFAnimChooserTable : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	// UAssetDefinition interface
	virtual FText GetAssetDisplayName() const override { return LOCTEXT("UAFAnimChooserTable", "UAF Anim Chooser Table"); }
	virtual const FSlateBrush* GetThumbnailBrush(const FAssetData& InAssetData, const FName InClassName) const override;
	virtual const FSlateBrush* GetIconBrush(const FAssetData& InAssetData, const FName InClassName) const override;
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(FColor(100,100,50)); }
	virtual TSoftClassPtr<UObject> GetAssetClass() const override { return UUAFAnimChooserTable::StaticClass(); }
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override
	{
		static const auto Categories = { FAssetCategoryPath(EAssetCategoryPaths::Animation, LOCTEXT("UAFSubMenu", "Animation Framework")) };
		return Categories;
	}
	virtual bool ShouldSaveExternalPackages() const override { return true; }
};

#undef LOCTEXT_NAMESPACE