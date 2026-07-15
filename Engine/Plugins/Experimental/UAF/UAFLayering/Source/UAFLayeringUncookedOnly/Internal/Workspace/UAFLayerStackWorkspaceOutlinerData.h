// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "AnimNextAssetWorkspaceAssetUserData.h"
#include "UAFLayerStackWorkspaceOutlinerData.generated.h"

USTRUCT()
struct FUAFLayerStackWorkspaceOutlinerData : public FAnimNextRigVMAssetOutlinerData
{
	GENERATED_BODY()
	
	FUAFLayerStackWorkspaceOutlinerData() = default;
};

USTRUCT()
struct FUAFLayerStackLayerOutlinerData : public FWorkspaceOutlinerItemData
{
	GENERATED_BODY()
	
	FUAFLayerStackLayerOutlinerData() = default;

	UPROPERTY()
	FName LayerName = NAME_None;
};
