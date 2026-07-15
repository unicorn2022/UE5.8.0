// Copyright Epic Games, Inc.All Rights Reserved.

#pragma once

#include "Misc/FrameRate.h"

namespace UE::CaptureData
{

UE_INTERNAL CAPTUREDATAUTILS_API FFrameRate EstimateSmpteTimecodeRate(const FFrameRate InMediaFrameRate);

}