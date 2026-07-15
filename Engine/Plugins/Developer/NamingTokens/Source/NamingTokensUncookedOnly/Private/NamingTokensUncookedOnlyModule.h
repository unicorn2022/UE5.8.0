// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

struct FAssetData;

class FNamingTokensUncookedOnlyModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
	void OnAssetAdded(const FAssetData& InAssetData);
	
private:
	FDelegateHandle AssetAddedHandle;
};
