// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


/**
 * Rendering synchronization protocol strings
 */
namespace DisplayClusterRenderSyncStrings
{
	extern const FString ProtocolName;

	extern const FString TypeRequest;
	extern const FString TypeResponse;

	extern const FString ArgumentsDefaultCategory;

	namespace SynchronizeOnBarrier
	{
		extern const FString Name;
	};
};
