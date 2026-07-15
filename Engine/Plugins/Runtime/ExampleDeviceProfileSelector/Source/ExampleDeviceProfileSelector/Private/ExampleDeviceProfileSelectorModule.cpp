// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExampleDeviceProfileSelectorModule.h"
#include "Modules/ModuleManager.h"


IMPLEMENT_MODULE(FExampleDeviceProfileSelectorModule, ExampleDeviceProfileSelector);


void FExampleDeviceProfileSelectorModule::StartupModule()
{
}


void FExampleDeviceProfileSelectorModule::ShutdownModule()
{
}


const FString FExampleDeviceProfileSelectorModule::GetRuntimeDeviceProfileName()
{
	FString ProfileName = FPlatformProperties::PlatformName();
	UE_LOGF(LogInit, Log, "Selected Device Profile: [%ls]", *ProfileName);
	return ProfileName;
}
