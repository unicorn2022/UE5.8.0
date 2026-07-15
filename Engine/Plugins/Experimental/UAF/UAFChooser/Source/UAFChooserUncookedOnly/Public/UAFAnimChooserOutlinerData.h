// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "WorkspaceAssetRegistryInfo.h"

#include "UAFAnimChooserOutlinerData.generated.h"

USTRUCT()
struct FUAFChooserOutlinerItemData : public FWorkspaceOutlinerItemData
{
	GENERATED_BODY()

	FUAFChooserOutlinerItemData() = default;

	UPROPERTY()
	FSoftObjectPath ObjectPath;

	UPROPERTY()
	bool bIsNestedObject = false;
	
	UPROPERTY()
	bool bExternalAsset = false;
};
