// Copyright Epic Games, Inc. All Rights Reserved.

#include "MicrosoftCommon.h"

#if PLATFORM_WINDOWS

#if WMFMEDIA_SUPPORTED_PLATFORM
	#pragma comment(lib, "mfplat")
	#pragma comment(lib, "mfuuid")
	#pragma comment(lib, "Mfreadwrite")
#endif

#endif // PLATFORM_WINDOWS
