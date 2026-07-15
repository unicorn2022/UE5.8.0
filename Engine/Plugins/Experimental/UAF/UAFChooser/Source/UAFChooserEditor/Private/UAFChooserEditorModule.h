// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "UObject/TopLevelAssetPath.h"

namespace UE::UAF::ChooserEditor
{

class FModule : public IModuleInterface
{
private:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
	FTopLevelAssetPath ChooserClassPath;
};

}
