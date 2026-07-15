// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "AssetDefinitionDefault.h"
#include "MediaSource.h"
#include "AssetDefinition_MediaSource.generated.h"

UCLASS(MinimalAPI)
class UAssetDefinition_MediaSource : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	// UAssetDefinition Begin
	virtual FText GetAssetDisplayName() const override { return FText::GetEmpty(); }
	virtual FLinearColor GetAssetColor() const override { return FColor::White; }
	virtual TSoftClassPtr<UObject> GetAssetClass() const override { return UMediaSource::StaticClass(); }
	MEDIAPLAYEREDITOR_API virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
	// UAssetDefinition End
};
