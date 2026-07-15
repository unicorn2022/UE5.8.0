// Copyright Epic Games, Inc. All Rights Reserved.

#include "CrossCompiler.h"

namespace CrossCompiler
{
	FCriticalSection* GetCrossCompilerLock()
	{
		static FCriticalSection HlslCcCs;
		return &HlslCcCs;
	}
}