// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowVolumeCoreModule.h"

#include "CoreMinimal.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Dataflow/OpenVDB.h"

#define LOCTEXT_NAMESPACE "DataflowCore"

void IDataflowVolumeCoreModule::StartupModule()
{
	// Initialize OpenVDB
	openvdb::initialize();
}


void IDataflowVolumeCoreModule::ShutdownModule()
{}

IMPLEMENT_MODULE(IDataflowVolumeCoreModule, DataflowVolumeCore)

#undef LOCTEXT_NAMESPACE
