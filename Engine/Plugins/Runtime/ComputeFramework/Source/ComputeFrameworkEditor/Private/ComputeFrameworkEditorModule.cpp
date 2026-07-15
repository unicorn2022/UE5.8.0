// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeFrameworkEditorModule.h"

#include "ComputeFramework/ComputeFrameworkCompilationTick.h"
#include "Modules/ModuleManager.h"

void FComputeFrameworkEditorModule::StartupModule()
{
	TickObject = MakeUnique<FComputeFrameworkCompilationTick>();
}

void FComputeFrameworkEditorModule::ShutdownModule()
{
	TickObject = nullptr;
}

IMPLEMENT_MODULE(FComputeFrameworkEditorModule, ComputeFrameworkEditor)
