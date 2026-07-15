// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Selectors/MetaHumanCalibrationSelector.h"

#include "MetaHumanDiagnosticsBasedSelector.generated.h"

#define UE_API METAHUMANCALIBRATIONDIAGNOSTICS_API

// Frame providers
UCLASS(BlueprintType, Blueprintable, Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories)
class UE_API UMetaHumanFeatureMatcherFrameProvider : public UObject
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "MetaHuman Calibration")
	TArray<int32> GetSelectedFrames() const;

protected:

	virtual TArray<int32> GetSelectedFrames_Implementation() const
		PURE_VIRTUAL(UMetaHumanFeatureMatcherFrameProvider::GetSelectedFrames_Implementation, return TArray<int32>(););
};

UCLASS(BlueprintType, Blueprintable)
class UMetaHumanManualFrameProvider final : public UMetaHumanFeatureMatcherFrameProvider
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman Calibration")
	TArray<int32> SelectedFrames;

	virtual TArray<int32> GetSelectedFrames_Implementation() const override { return SelectedFrames; }
};

UCLASS(BlueprintType, Blueprintable)
class UMetaHumanDiagnosticsBasedSelectorSettings final : public UMetaHumanCalibrationSelectorSettings
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "MetaHuman Calibration")
	TObjectPtr<UMetaHumanFeatureMatcherFrameProvider> FrameProvider;
};

UCLASS(BlueprintType, Blueprintable)
class UMetaHumanDiagnosticsBasedSelector final : public UMetaHumanCalibrationSelector
{
	GENERATED_BODY()

public:
	
	virtual TArray<UCameraCalibration*> OrderCalibrations_Implementation(UFootageCaptureData* InCaptureData, const TArray<UCameraCalibration*>& InCameraCalibrations) const override;
	virtual TSubclassOf<UMetaHumanCalibrationSelectorSettings> GetSettingsClass_Implementation() const override;

private:

	static constexpr int32 MaxNumberOfThreads = 8;
};

#undef UE_API