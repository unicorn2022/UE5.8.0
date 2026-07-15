// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Interfaces/IChunkDeltaOptimiser.h"
#include "BuildPatchSettings.h"

namespace BuildPatchServices
{
	class FChunkDeltaOptimiserFactory
	{
	public:
		static IChunkDeltaOptimiser* Create(FChunkDeltaOptimiserConfiguration Configuration);
	};
}
