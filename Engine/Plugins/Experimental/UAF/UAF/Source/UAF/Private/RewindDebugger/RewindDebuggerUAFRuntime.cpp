// Copyright Epic Games, Inc. All Rights Reserved.

#include "RewindDebuggerUAFRuntime.h"

#if UAF_TRACE_ENABLED

namespace UE::UAF
{
	
void FRewindDebuggerUAFRuntime::RecordingStarted()
{
	UE::Trace::ToggleChannel(TEXT("UAF"), true);
}

void FRewindDebuggerUAFRuntime::RecordingStopped()
{
	UE::Trace::ToggleChannel(TEXT("UAF"), false);
}

void FRewindDebuggerUAFRuntime::Clear()
{
	FUAFTrace::Reset();
}

}

#endif
	
