// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Selectors/MetaHumanCalibrationSelector.h"

#include "MetaHumanTimecodeBasedSelector.generated.h"

#define UE_API METAHUMANCALIBRATIONCORE_API

UCLASS(BlueprintType, Blueprintable)
class UMetaHumanTimecodeBasedSelector final : public UMetaHumanCalibrationSelector
{
	GENERATED_BODY()

public:
	virtual TArray<UCameraCalibration*> OrderCalibrations_Implementation(UFootageCaptureData* InCaptureData, const TArray<UCameraCalibration*>& InCameraCalibrations) const override;
	virtual TSubclassOf<UMetaHumanCalibrationSelectorSettings> GetSettingsClass_Implementation() const override;
};

#undef UE_API