// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"

#include "FixSoundWaveTimecodeInfoCommandlet.generated.h"

/**
 * Internal commandlet to fix any USoundWaves with corrupt TimecodeInfo.
 */
UCLASS()
class UFixSoundWaveTimecodeInfoCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UFixSoundWaveTimecodeInfoCommandlet();

	virtual int32 Main(const FString& Params);
};
