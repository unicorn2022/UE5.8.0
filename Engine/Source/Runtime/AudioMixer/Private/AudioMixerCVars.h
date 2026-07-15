// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

namespace AudioMixerCVars
{
	// This is used to decide whether or not to create a render scheduler when the mixer device is created.
	//  Most code should test FMixerDevice::UseRenderScheduler() instead of using this.
	extern int32 UseRenderScheduler;
}
