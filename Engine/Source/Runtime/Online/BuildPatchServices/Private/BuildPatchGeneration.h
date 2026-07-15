// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Interfaces/IDirectoryChunker.h"
#include "BuildPatchSettings.h"

namespace BuildPatchServices
{
	class FDirectoryChunkerFactory
	{
	public:
		static IDirectoryChunker* Create(FDirectoryChunkerConfiguration Configuration);
	};
}
