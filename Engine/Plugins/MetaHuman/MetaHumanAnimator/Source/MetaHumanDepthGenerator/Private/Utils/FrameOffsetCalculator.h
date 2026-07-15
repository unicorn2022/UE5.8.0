// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Timecode.h"
#include "Misc/FrameRate.h"
#include "CoreMinimal.h"

namespace UE::MetaHuman::DepthGenerator
{

struct FCameraTimecodeInfo
{
	FTimecode Timecode;
	FFrameRate FrameRate;
};

TArray<int32> CalculateFrameOffset(const TArray<FCameraTimecodeInfo>& InCameraTimecodeInfos);

}