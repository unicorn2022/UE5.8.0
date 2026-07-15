// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "CaptureData.h"
#include "MetaHumanCalibrationGeneratorConfig.h"
#include "MetaHumanCalibrationGeneratorOptions.h"

#include "MetaHumanCalibrationBatchLibrary.generated.h"


/** Result for a single capture-data asset processed in a batch calibration run. */
USTRUCT(BlueprintType)
struct FMetaHumanCalibrationBatchResult
{
	GENERATED_BODY()

	/** The capture data asset that was processed. */
	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman|Calibration|Batch")
	TObjectPtr<UFootageCaptureData> CaptureData = nullptr;

	/** Whether calibration succeeded for this asset. */
	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman|Calibration|Batch")
	bool bSuccess = false;

	/** Error message when bSuccess is false. */
	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman|Calibration|Batch")
	FString ErrorMessage;

	/** RMS reprojection error of the generated calibration (lower is better). Zero on failure. */
	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman|Calibration|Batch")
	double RMSError = 0.0;

	/** Content-browser path to the created CameraCalibration asset. Empty on failure. */
	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman|Calibration|Batch")
	FString CalibrationAssetPath;
};

/**
 * Blueprint function library for batch camera calibration generation.
 *
 * Provides three levels of API:
 * - BatchGenerateCalibration: Simple interface — just assets and board config.
 * - BatchGenerateCalibrationWithOptions: Advanced interface with per-asset Options.
 * - ConstructDefaultOptionsForCaptureData: Utility to build default Options for a CaptureData asset.
 *
 * Cancellation is supported via the FScopedSlowTask progress dialog.
 */
UCLASS()
class UMetaHumanCalibrationBatchLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Construct a default UMetaHumanCalibrationGeneratorOptions for a CaptureData asset.
	 *
	 * Sets AssetName to CC_<CaptureDataName>, PackagePath to the same directory as the
	 * CaptureData asset, AreaOfInterest to full frame bounds per camera, SharpnessThreshold
	 * to 5.0, and bAutoSaveAssets to true. SelectedFrames is left empty so that automatic
	 * frame selection will be used.
	 *
	 * The returned Options can be customised before passing to BatchGenerateCalibrationWithOptions.
	 *
	 * @param CaptureData The capture data asset to configure options for.
	 * @return A new Options object with sensible defaults, or nullptr if CaptureData is null.
	 */
	UFUNCTION(BlueprintCallable,
		Category = "MetaHuman|Calibration|Batch",
		meta = (DisplayName = "Construct Default Options For Capture Data"))
	static UMetaHumanCalibrationGeneratorOptions* ConstructDefaultOptionsForCaptureData(
		const UFootageCaptureData* CaptureData);

	/**
	 * Run calibration generation across multiple UFootageCaptureData assets with automatic defaults.
	 *
	 * For each asset, constructs default Options via ConstructDefaultOptionsForCaptureData and
	 * runs calibration with automatic frame selection. Output is placed next to each CaptureData
	 * asset. This is the simplest entry point for batch processing.
	 *
	 * @param CaptureDataAssets Footage capture data assets to process.
	 * @param BoardConfig      Calibration board pattern configuration (dimensions and square size).
	 * @return Per-asset results in input order.
	 */
	UFUNCTION(BlueprintCallable,
		Category = "MetaHuman|Calibration|Batch",
		meta = (DisplayName = "Batch Generate Camera Calibration"))
	static TArray<FMetaHumanCalibrationBatchResult> BatchGenerateCalibration(
		const TArray<UFootageCaptureData*>& CaptureDataAssets,
		const UMetaHumanCalibrationGeneratorConfig* BoardConfig);

	/**
	 * Run calibration generation with per-asset Options for full control.
	 *
	 * Each CaptureData asset is paired with a corresponding Options object. If an Options
	 * object has empty SelectedFrames, automatic frame selection is used and the
	 * SelectedFrames array on the Options object will be populated.
	 *
	 * @param CaptureDataAssets Footage capture data assets to process.
	 * @param BoardConfig      Calibration board pattern configuration (dimensions and square size).
	 * @param OptionsPerAsset  Per-asset options (must be same length as InCaptureDataAssets).
	 * @return Per-asset results in input order.
	 */
	UFUNCTION(BlueprintCallable,
		Category = "MetaHuman|Calibration|Batch",
		meta = (DisplayName = "Batch Generate Camera Calibration With Options"))
	static TArray<FMetaHumanCalibrationBatchResult> BatchGenerateCalibrationWithOptions(
		const TArray<UFootageCaptureData*>& CaptureDataAssets,
		const UMetaHumanCalibrationGeneratorConfig* BoardConfig,
		const TArray<UMetaHumanCalibrationGeneratorOptions*>& OptionsPerAsset);
};
