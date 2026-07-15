// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


struct FDisplayClusterClusterEventJson;


/**
 * Auxiliary class. Responsible for executing console commands.
 */
class DISPLAYCLUSTER_API FDisplayClusterConsoleExec
{
public:

	/** Runs console commands from JSON cluster events */
	static bool Exec(const FDisplayClusterClusterEventJson& InEvent);

	/** Generic console command runner */
	static bool Exec(const FString& InExecutorName, const FString& InExecCommand);
};
