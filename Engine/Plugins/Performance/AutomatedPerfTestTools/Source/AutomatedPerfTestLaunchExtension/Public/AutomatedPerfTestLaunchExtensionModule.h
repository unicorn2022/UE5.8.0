// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "ProjectLauncherModule.h"
#include "AutomatedPerfTestLaunchExtension.h"

class FAutomatedPerfTestLaunchExtensionModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TArray<TSharedRef<ProjectLauncher::FLaunchExtension>> Extensions;
};



