// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FSpawnTabArgs;
class SDockTab;

class FPlayerInputDebuggerModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);

	FDelegateHandle MenusStartupHandle;
};
