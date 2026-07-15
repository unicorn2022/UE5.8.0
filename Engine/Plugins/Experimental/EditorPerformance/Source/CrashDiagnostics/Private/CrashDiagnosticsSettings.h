// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CrashDiagnosticsSettings.generated.h"

UCLASS(MinimalAPI, config=EditorPerProjectUserSettings)
class UCrashDiagnosticsSettings : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * The start time of the last editor session.
	 * Used to detect crashes that occurred during the previous session.
	 */
	UPROPERTY(config)
	FDateTime LastSessionStartTime = FDateTime();
};
