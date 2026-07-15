// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextAnimGraphWorkspaceAssetUserData.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "UAFLayerStackWorkspaceAssetUserData.generated.h"

UCLASS()
class UUAFLayerStackWorkspaceAssetUserData : public UAnimNextAnimGraphWorkspaceAssetUserData
{
	GENERATED_BODY()

protected:
	// Begin UAnimNextAnimGraphWorkspaceAssetUserData Interface
	virtual void GetRootAssetExport(FAssetRegistryTagsContext Context) const override;
	virtual void GetWorkspaceAssetExports(FAssetRegistryTagsContext Context) const override;
	// End UAnimNextAnimGraphWorkspaceAssetUserData Interface

};