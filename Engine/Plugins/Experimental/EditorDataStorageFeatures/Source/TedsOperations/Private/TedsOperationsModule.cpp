// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsOperationsModule.h"

#include "Modules/ModuleManager.h"

namespace UE::Editor::DataStorage::Operations
{

void FTedsOperationsModule::StartupModule()
{
	IModuleInterface::StartupModule();
}

void FTedsOperationsModule::ShutdownModule()
{
	IModuleInterface::ShutdownModule();
}

}

IMPLEMENT_MODULE(UE::Editor::DataStorage::Operations::FTedsOperationsModule, TedsOperations);
