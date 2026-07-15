// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassRewindDebuggerIntegration.h"
#include "Trace/Trace.h"

void FMassRewindDebuggerRuntimeExtension::RecordingStarted()
{
	UE::Trace::ToggleChannel(TEXT("MassChannel"), true);
}

void FMassRewindDebuggerRuntimeExtension::RecordingStopped()
{
	UE::Trace::ToggleChannel(TEXT("MassChannel"), false);
}
