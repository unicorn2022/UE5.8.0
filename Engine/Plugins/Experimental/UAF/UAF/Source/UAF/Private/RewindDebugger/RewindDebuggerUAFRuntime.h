// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "RewindDebuggerRuntimeInterface/IRewindDebuggerRuntimeExtension.h"
#include "RewindDebugger/UAFTrace.h"

#if UAF_TRACE_ENABLED
// Rewind debugger extension for Chooser support

namespace UE::UAF
{

class FRewindDebuggerUAFRuntime : public IRewindDebuggerRuntimeExtension
{
public:
	virtual void RecordingStarted() override;
	virtual void RecordingStopped() override;
	
	virtual void Clear() override;
};

}

#endif