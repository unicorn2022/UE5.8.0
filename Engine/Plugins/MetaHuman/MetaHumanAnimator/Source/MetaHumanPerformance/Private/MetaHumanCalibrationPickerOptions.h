// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CameraCalibration.h"
#include "MetaHumanCalibrationPickerOptions.generated.h"

/** Options object shown in the calibration picker dialog's DetailsView. */
UCLASS()
class UMetaHumanCalibrationPickerOptions : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Calibration")
	TObjectPtr<UCameraCalibration> CameraCalibration;
};
