// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if PLATFORM_MAC
#include "Mac/MacSystemIncludes.h"
#elif PLATFORM_IOS
#include "IOS/IOSSystemIncludes.h"
#endif

struct FAppleDebugEvent
{
	void const* Tag;
	uint32 Color;
	uint64 Code;
	dispatch_block_t Destructor;
};