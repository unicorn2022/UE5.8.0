// Copyright Epic Games, Inc. All Rights Reserved.
#include "LiveLinkGenericRecordingDeviceModule.h"

#define LOCTEXT_NAMESPACE "FLiveLinkGenericRecordingDeviceModule"

void FLiveLinkGenericRecordingDeviceModule::StartupModule()
{
	UE_LOGF(LogTemp, Verbose, "LiveLinkGenericRecordingDevice module started");
}

void FLiveLinkGenericRecordingDeviceModule::ShutdownModule()
{
	UE_LOGF(LogTemp, Verbose, "LiveLinkGenericRecordingDevice module shutdown");
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FLiveLinkGenericRecordingDeviceModule, LiveLinkGenericRecordingDevice)
