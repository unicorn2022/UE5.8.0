// Copyright Epic Games, Inc. All Rights Reserved.

#include "VEUV/VEUVDebugCapture.h"

VEUV::FDebugHistory& VEUV::FDebugHistory::Get()
{
	static FDebugHistory Instance;
	return Instance;
}
