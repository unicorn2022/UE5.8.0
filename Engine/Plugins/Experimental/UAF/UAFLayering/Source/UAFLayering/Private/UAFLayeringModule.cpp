// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAFLayeringModule.h"

#include "Factory/AnimNextFactoryParams.h"
#include "RigVMCore/RigVMRegistry.h"
#include "Traits/LayerDataProviderTraitData.h"

#define LOCTEXT_NAMESPACE "FUAFLayeringModule"

void FUAFLayeringModule::StartupModule()
{
	static UScriptStruct* const AllowedStructTypes[] =
	{
		FUAFLayerProperties::StaticStruct(),
		FAnimNextFactoryParams::StaticStruct(),
	};
	
	FRigVMRegistry::Get().RegisterStructTypes(AllowedStructTypes);
}

void FUAFLayeringModule::ShutdownModule()
{
	
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FUAFLayeringModule, UAFLayering)
