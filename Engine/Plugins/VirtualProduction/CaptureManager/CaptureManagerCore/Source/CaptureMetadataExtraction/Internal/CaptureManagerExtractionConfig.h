// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"

namespace UE::CaptureManager
{

struct FExtractionConfig
{
	bool bUseFFprobe = false;
	FString FFmpegPath;
};

} // namespace UE::CaptureManager
