// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCalibrationBatchLibrary.h"
#include "MetaHumanCalibrationGenerator.h"
#include "MetaHumanCalibrationAutoFrameSelector.h"

#include "ImageSequenceUtils.h"
#include "ImgMediaSource.h"

#include "CoreGlobals.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogMetaHumanCalibrationBatch, Log, All);

#define LOCTEXT_NAMESPACE "MetaHumanCalibrationBatch"

UMetaHumanCalibrationGeneratorOptions* UMetaHumanCalibrationBatchLibrary::ConstructDefaultOptionsForCaptureData(
	const UFootageCaptureData* InCaptureData)
{
	if (!InCaptureData)
	{
		UE_LOGF(LogMetaHumanCalibrationBatch, Error, "ConstructDefaultOptionsForCaptureData: InCaptureData is null.");
		return nullptr;
	}

	UMetaHumanCalibrationGeneratorOptions* Options = NewObject<UMetaHumanCalibrationGeneratorOptions>();

	Options->AssetName = FString::Printf(TEXT("CC_%s"), *InCaptureData->GetName());

	// Place output next to the CaptureData asset. The CC_ prefix on AssetName
	// distinguishes calibration output from source data.
	Options->PackagePath.Path = FPackageName::GetLongPackagePath(InCaptureData->GetPackage()->GetName());

	Options->bAutoSaveAssets = true;
	Options->SharpnessThreshold = 5.0f;

	// Set AreaOfInterest to full frame bounds for each camera.
	for (const UImgMediaSource* ImageSource : InCaptureData->ImageSequences)
	{
		FMetaHumanAreaOfInterest AOI;

		if (IsValid(ImageSource))
		{
			FIntVector2 Dimensions;
			int32 NumImages = 0;
			if (FImageSequenceUtils::GetImageSequenceInfoFromAsset(ImageSource, Dimensions, NumImages))
			{
				AOI.BottomRight = FVector2D(Dimensions.X, Dimensions.Y);
			}
			else
			{
				UE_LOGF(LogMetaHumanCalibrationBatch, Warning,
					"ConstructDefaultOptionsForCaptureData: Failed to get image sequence info for '%ls' in '%ls' - AOI will default to zero bounds.",
					*ImageSource->GetName(), *InCaptureData->GetName());
			}
		}
		else
		{
			UE_LOGF(LogMetaHumanCalibrationBatch, Warning,
				"ConstructDefaultOptionsForCaptureData: ImageSequence entry is null or invalid in '%ls' - AOI will default to zero bounds.",
				*InCaptureData->GetName());
		}

		Options->AreaOfInterestsForCameras.Add(AOI);
	}

	return Options;
}

TArray<FMetaHumanCalibrationBatchResult> UMetaHumanCalibrationBatchLibrary::BatchGenerateCalibration(
	const TArray<UFootageCaptureData*>& InCaptureDataAssets,
	const UMetaHumanCalibrationGeneratorConfig* InBoardConfig)
{
	if (InCaptureDataAssets.IsEmpty())
	{
		return {};
	}

	// Build per-asset Options using defaults derived from each CaptureData asset.
	TArray<UMetaHumanCalibrationGeneratorOptions*> OptionsPerAsset;
	OptionsPerAsset.Reserve(InCaptureDataAssets.Num());

	for (UFootageCaptureData* CaptureData : InCaptureDataAssets)
	{
		OptionsPerAsset.Add(ConstructDefaultOptionsForCaptureData(CaptureData));
	}

	TArray<FMetaHumanCalibrationBatchResult> Results =
		BatchGenerateCalibrationWithOptions(InCaptureDataAssets, InBoardConfig, OptionsPerAsset);

	// The Options we created are only used during the synchronous call above and are safe to discard.
	for (UMetaHumanCalibrationGeneratorOptions* Opts : OptionsPerAsset)
	{
		if (Opts)
		{
			Opts->MarkAsGarbage();
		}
	}

	return Results;
}

TArray<FMetaHumanCalibrationBatchResult> UMetaHumanCalibrationBatchLibrary::BatchGenerateCalibrationWithOptions(
	const TArray<UFootageCaptureData*>& InCaptureDataAssets,
	const UMetaHumanCalibrationGeneratorConfig* InBoardConfig,
	const TArray<UMetaHumanCalibrationGeneratorOptions*>& InOptionsPerAsset)
{
	TArray<FMetaHumanCalibrationBatchResult> Results;

	if (InCaptureDataAssets.IsEmpty())
	{
		return Results;
	}

	if (!InBoardConfig)
	{
		UE_LOGF(LogMetaHumanCalibrationBatch, Error, "BatchGenerateCalibrationWithOptions: InBoardConfig must not be null.");
		return Results;
	}

	if (InCaptureDataAssets.Num() != InOptionsPerAsset.Num())
	{
		UE_LOGF(LogMetaHumanCalibrationBatch, Error,
			"BatchGenerateCalibrationWithOptions: InCaptureDataAssets (%d) and InOptionsPerAsset (%d) must be the same length.",
			InCaptureDataAssets.Num(), InOptionsPerAsset.Num());
		return Results;
	}

	// Validate the board config once up front rather than failing N times inside the loop.
	{
		TValueOrError<void, FString> ConfigValid = InBoardConfig->CheckConfigValidity();
		if (ConfigValid.HasError())
		{
			UE_LOGF(LogMetaHumanCalibrationBatch, Error,
				"BatchGenerateCalibrationWithOptions: Board config is invalid: %ls",
				*ConfigValid.GetError());
			return Results;
		}
	}

	const int32 NumAssets = InCaptureDataAssets.Num();
	Results.Reserve(NumAssets);

	FScopedSlowTask BatchProgress(
		static_cast<float>(NumAssets),
		LOCTEXT("BatchCalibProgressTitle", "Generating Calibrations..."));
	BatchProgress.MakeDialog(true);


	for (int32 Index = 0; Index < NumAssets; ++Index)
	{
		if (BatchProgress.ShouldCancel() || IsEngineExitRequested())
		{
			UE_LOGF(LogMetaHumanCalibrationBatch, Warning,
				"BatchGenerateCalibrationWithOptions: Cancelled by user after %d/%d assets.",
				Index, NumAssets);
			for (int32 Remaining = Index; Remaining < NumAssets; ++Remaining)
			{
				FMetaHumanCalibrationBatchResult& Cancelled = Results.AddDefaulted_GetRef();
				Cancelled.CaptureData = InCaptureDataAssets[Remaining];
				Cancelled.ErrorMessage = TEXT("Batch cancelled by user.");
			}
			break;
		}

		UFootageCaptureData* CaptureData = InCaptureDataAssets[Index];
		const int32 AssetNumber = Index + 1;
		FMetaHumanCalibrationBatchResult& Result = Results.AddDefaulted_GetRef();
		Result.CaptureData = CaptureData;

		if (!CaptureData)
		{
			Result.ErrorMessage = TEXT("CaptureData asset is null - skipped.");
			BatchProgress.EnterProgressFrame(1.f,
				FText::Format(LOCTEXT("BatchCalibSkipped", "Skipping null asset ({0}/{1})"),
					FText::AsNumber(AssetNumber), FText::AsNumber(NumAssets)));
			continue;
		}

		if (!InOptionsPerAsset[Index])
		{
			Result.ErrorMessage = TEXT("Options object is null - skipped.");
			BatchProgress.EnterProgressFrame(1.f,
				FText::Format(LOCTEXT("BatchCalibSkippedOptions", "Skipping null options ({0}/{1})"),
					FText::AsNumber(AssetNumber), FText::AsNumber(NumAssets)));
			continue;
		}

		BatchProgress.EnterProgressFrame(1.f,
			FText::Format(LOCTEXT("BatchCalibProcessing", "Processing {0} ({1}/{2})"),
				FText::FromString(CaptureData->GetName()),
				FText::AsNumber(AssetNumber),
				FText::AsNumber(NumAssets)));

		UMetaHumanCalibrationGeneratorOptions* AssetOptions = InOptionsPerAsset[Index];

		// Automatic frame selection when no frames are pre-populated.
		if (AssetOptions->SelectedFrames.IsEmpty())
		{
			UMetaHumanCalibrationAutoFrameSelector* FrameSelector =
				NewObject<UMetaHumanCalibrationAutoFrameSelector>();
			ON_SCOPE_EXIT { FrameSelector->MarkAsGarbage(); };

			TArray<int32> SelectedFrames = FrameSelector->Run(CaptureData, InBoardConfig, AssetOptions);

			if (SelectedFrames.IsEmpty())
			{
				Result.ErrorMessage = TEXT("Auto frame selection returned no usable frames.");
				UE_LOGF(LogMetaHumanCalibrationBatch, Warning,
					"BatchGenerateCalibrationWithOptions: No frames selected for '%ls'.",
					*CaptureData->GetName());
				continue;
			}

			AssetOptions->SelectedFrames = MoveTemp(SelectedFrames);
		}

		// Each asset gets its own generator instance - the generator is stateful
		// (bInitialized / bCamerasConfigured) and must not be reused across assets.
		UMetaHumanCalibrationGenerator* Generator = NewObject<UMetaHumanCalibrationGenerator>();
		ON_SCOPE_EXIT { Generator->MarkAsGarbage(); };

		if (!Generator->Init(InBoardConfig))
		{
			Result.ErrorMessage = Generator->GetLastError();
			UE_LOGF(LogMetaHumanCalibrationBatch, Warning,
				"BatchGenerateCalibrationWithOptions: Init failed for '%ls': %ls",
				*CaptureData->GetName(), *Result.ErrorMessage);
			continue;
		}

		const bool bProcessOk = Generator->Process(CaptureData, AssetOptions);
		Result.bSuccess = bProcessOk;
		Result.RMSError = Generator->GetLastRMSError();

		if (bProcessOk)
		{
			if (!CaptureData->CameraCalibrations.IsEmpty())
			{
				if (const UCameraCalibration* CreatedAsset = CaptureData->CameraCalibrations.Last())
				{
					Result.CalibrationAssetPath = CreatedAsset->GetPackage()->GetName();
				}
			}
		}
		else
		{
			Result.ErrorMessage = Generator->GetLastError();
			UE_LOGF(LogMetaHumanCalibrationBatch, Warning,
				"BatchGenerateCalibrationWithOptions: Process failed for '%ls': %ls",
				*CaptureData->GetName(), *Result.ErrorMessage);
		}
	}

	return Results;
}

#undef LOCTEXT_NAMESPACE
