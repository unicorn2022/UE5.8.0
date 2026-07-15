// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FSpawnTabArgs;
class SDockTab;

//======================================================================================================================
class FControlRigDynamicsEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedRef<SDockTab> OnSpawnDebugTab(const FSpawnTabArgs& SpawnTabArgs);
	void RegisterMenus();
};
