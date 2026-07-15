// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RewindDebuggerRuntimeInterface/IRewindDebuggerRuntimeExtension.h"

/**
 * Runtime extension that enables/disables the Mass trace channel
 * when RewindDebugger recording starts/stops.
 */
class FMassRewindDebuggerRuntimeExtension final : public IRewindDebuggerRuntimeExtension
{
protected:
	virtual void RecordingStarted() override;
	virtual void RecordingStopped() override;
};
