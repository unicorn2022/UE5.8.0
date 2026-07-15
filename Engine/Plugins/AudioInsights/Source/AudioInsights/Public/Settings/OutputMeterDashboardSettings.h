// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "OutputMeterDashboardSettings.generated.h"

USTRUCT()
struct FOutputMeterDashboardSettings
{
	GENERATED_BODY()

	UPROPERTY(config)
	float LoudnessMetersSlotSize = 0.75f;

	UPROPERTY(config)
	float RMSMeterSlotSize = 0.25f;

	UPROPERTY(config)
	bool bLoudnessMetersCollapsed = false;

	UPROPERTY(config)
	bool bRMSMeterCollapsed = false;
};
