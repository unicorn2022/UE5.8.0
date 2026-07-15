// Copyright Epic Games, Inc. All Rights Reserved.

#include "AutomatedPerfTestLaunchExtensionModule.h"

void FAutomatedPerfTestLaunchExtensionModule::StartupModule() 
{
	Extensions.Add(MakeShared<FAutomatedPerfTestLaunchExtension>());
	for (TSharedRef<ProjectLauncher::FLaunchExtension> Extension : Extensions)
	{
		IProjectLauncherModule::Get().RegisterExtension(Extension);
	}
}

void FAutomatedPerfTestLaunchExtensionModule::ShutdownModule() 
{
	if (IProjectLauncherModule* ProjectLauncher = IProjectLauncherModule::TryGet())
	{
		for (TSharedRef<ProjectLauncher::FLaunchExtension> Extension : Extensions)
		{
			ProjectLauncher->UnregisterExtension(Extension);
		}
	}

	Extensions.Reset();
}

IMPLEMENT_MODULE(FAutomatedPerfTestLaunchExtensionModule, AutomatedPerfTestLaunchExtension);

