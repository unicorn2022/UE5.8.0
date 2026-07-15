// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selectors/MetaHumanCalibrationSelector.h"

#include "CaptureData.h"
#include "CameraCalibration.h"

UCameraCalibration* UMetaHumanCalibrationSelector::SelectCalibration_Implementation(UFootageCaptureData* InCaptureData, const TArray<UCameraCalibration*>& InCameraCalibrations) const
{
	TArray<UCameraCalibration*> OrderedCalibration = OrderCalibrations(InCaptureData, InCameraCalibrations);
	if (OrderedCalibration.IsEmpty())
	{
		return nullptr;
	}

	// Get first calibration
	return OrderedCalibration[0];
}
