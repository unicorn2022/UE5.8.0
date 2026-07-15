// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "Concepts/DerivedFrom.h"

#include "MetaHumanCalibrationSelector.generated.h"

#define UE_API METAHUMANCALIBRATIONCORE_API

class UFootageCaptureData;
class UCameraCalibration;

UCLASS(BlueprintType, Blueprintable, MinimalAPI, Abstract)
class UMetaHumanCalibrationSelectorSettings : public UObject
{
	GENERATED_BODY()
};

UCLASS(BlueprintType, Blueprintable, Abstract)
class UE_API UMetaHumanCalibrationSelector : public UObject
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, BlueprintPure = false, Category = "MetaHuman Calibration")
	UCameraCalibration* SelectCalibration(UFootageCaptureData* CaptureData, const TArray<UCameraCalibration*>& CameraCalibrations) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, BlueprintPure = false, Category = "MetaHuman Calibration")
	TArray<UCameraCalibration*> OrderCalibrations(UFootageCaptureData* CaptureData, const TArray<UCameraCalibration*>& CameraCalibrations) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "MetaHuman Calibration")
	TSubclassOf<UMetaHumanCalibrationSelectorSettings> GetSettingsClass() const;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Calibration")
	void SetSettings(UMetaHumanCalibrationSelectorSettings* Settings) { SelectorSettings = MoveTemp(Settings); }

protected:

	virtual UCameraCalibration* SelectCalibration_Implementation(UFootageCaptureData* InCaptureData, const TArray<UCameraCalibration*>& InCameraCalibrations) const;

	virtual TArray<UCameraCalibration*> OrderCalibrations_Implementation(UFootageCaptureData* InCaptureData, const TArray<UCameraCalibration*>& InCameraCalibrations) const
		PURE_VIRTUAL(UMetaHumanCalibrationSelector::OrderCalibrations_Implementation, return TArray<UCameraCalibration*>(););
	virtual TSubclassOf<UMetaHumanCalibrationSelectorSettings> GetSettingsClass_Implementation() const
		PURE_VIRTUAL(UMetaHumanCalibrationSelector::GetSettingsClass_Implementation, return nullptr;);

public:

	template<typename T = UMetaHumanCalibrationSelectorSettings>
		requires UE::CDerivedFrom<T, UMetaHumanCalibrationSelectorSettings>
	const T* GetSettings() const { return Cast<T>(SelectorSettings); }

	template<typename T = UMetaHumanCalibrationSelectorSettings>
		requires UE::CDerivedFrom<T, UMetaHumanCalibrationSelectorSettings>
	T* GetSettings() { return Cast<T>(SelectorSettings); }

private:

	UPROPERTY()
	TObjectPtr<UMetaHumanCalibrationSelectorSettings> SelectorSettings;
};

#undef UE_API