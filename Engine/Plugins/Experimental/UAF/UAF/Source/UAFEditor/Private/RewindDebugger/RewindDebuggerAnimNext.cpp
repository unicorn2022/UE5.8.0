// Copyright Epic Games, Inc. All Rights Reserved.

#include "RewindDebuggerAnimNext.h"

#if UAF_TRACE_ENABLED
#include "IGameplayProvider.h"

FRewindDebuggerUAF::FRewindDebuggerUAF()
{

}

void FRewindDebuggerUAF::Update(float DeltaTime, IRewindDebugger* RewindDebugger)
{
	// Future place to sync anim next workspace editor debugging when scrubbing
}

#endif // UAF_TRACE_ENABLED
