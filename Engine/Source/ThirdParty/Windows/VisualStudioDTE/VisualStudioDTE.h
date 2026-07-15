// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"
#include <unknwn.h>

#if WITH_VISUALSTUDIO_DTE
	THIRD_PARTY_INCLUDES_START
	#pragma warning(push)
	#pragma warning(disable: 4278)
	#pragma warning(disable: 4471)
	#pragma warning(disable: 4146)
	#pragma warning(disable: 6244)
	PRAGMA_DISABLE_CAST_FUNCTION_TYPE_MISMATCH_WARNINGS
	
	#include "dte80a.tlh"

	PRAGMA_ENABLE_CAST_FUNCTION_TYPE_MISMATCH_WARNINGS
	#pragma warning(pop)
	THIRD_PARTY_INCLUDES_END
#endif

#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"
