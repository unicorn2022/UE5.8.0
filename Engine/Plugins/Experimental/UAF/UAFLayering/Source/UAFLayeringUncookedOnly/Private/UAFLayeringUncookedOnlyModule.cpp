// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAFLayeringUncookedOnlyModule.h"

#define LOCTEXT_NAMESPACE "FUAFLayeringEditorModule"

namespace UE::UAF::Layering
{
void FUAFLayeringUncookedOnlyModule::StartupModule()
{
}

void FUAFLayeringUncookedOnlyModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FUAFLayeringUncookedOnlyModule, UAFLayeringUncookedOnly)
}

#undef LOCTEXT_NAMESPACE
	
