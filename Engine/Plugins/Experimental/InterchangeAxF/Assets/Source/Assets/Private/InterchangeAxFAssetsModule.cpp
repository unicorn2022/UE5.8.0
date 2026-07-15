// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class FInterchangeAxFAssetsModule : public IModuleInterface
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override
	{}

	virtual void ShutdownModule() override
	{}
};

IMPLEMENT_MODULE(FInterchangeAxFAssetsModule, InterchangeAxFAssets)

