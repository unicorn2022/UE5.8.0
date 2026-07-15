// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "LoudnessMeterSettings.h"
#include "Styling/StyleColors.h"

#include "LoudnessMeterRackUnitSettings.generated.h"

USTRUCT()
struct FLoudnessMetricDisplayOptions : public FLoudnessMeterDisplayOptions
{
	GENERATED_BODY()
};

USTRUCT()
struct FLoudnessMeterRackUnitSettings : public FLoudnessMeterSettings
{
	GENERATED_BODY()
};
