// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioCaptureCoreLog.h"

#define WASAPI_CAPTURE_LOG_RESULT(FunctionName, Result) \
	{ \
		FString ErrorString = FString::Printf(TEXT("%s -> 0x%X (line: %d)"), TEXT( FunctionName ), (uint32)Result, __LINE__); \
		UE_LOGF(LogAudioCaptureCore, Error, "WasapiCapture Error: %ls", *ErrorString);				  \
	}
