// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selectors/MetaHumanTimecodeBasedSelector.h"

#include "CaptureData.h"
#include "CameraCalibration.h"

#include "ImgMediaSource.h"
#include "CameraCalibrationMetadata.h"

DEFINE_LOG_CATEGORY_STATIC(LogTimecodeBasedCalibrationSelector, Log, All)

TArray<UCameraCalibration*> UMetaHumanTimecodeBasedSelector::OrderCalibrations_Implementation(UFootageCaptureData* InCaptureData, const TArray<UCameraCalibration*>& InCameraCalibrations) const
{
	TArray<UCameraCalibration*> OrderedArray;

	if (!InCaptureData)
	{
		UE_LOGF(LogTimecodeBasedCalibrationSelector, Error, "Footage Capture Data isn't provided");
		return OrderedArray;
	}

	if (InCaptureData->ImageSequences.IsEmpty())
	{
		UE_LOGF(LogTimecodeBasedCalibrationSelector, Error, "No image sequences found in the Footage Capture Data [%ls]", *InCaptureData->GetName());
		return OrderedArray;
	}

	if (!InCaptureData->ImageSequences[0])
	{
		UE_LOGF(LogTimecodeBasedCalibrationSelector, Error, "Image sequences can't be null in the Footage Capture Data [%ls]", *InCaptureData->GetName());
		return OrderedArray;
	}

	FTimecode Timecode = InCaptureData->ImageSequences[0]->StartTimecode;
	FFrameRate FrameRate = InCaptureData->ImageSequences[0]->FrameRateOverride;

	if (!Timecode.IsValid() || !FrameRate.IsValid() || FrameRate.Numerator == 0)
	{
		UE_LOGF(LogTimecodeBasedCalibrationSelector, Error, "Footage Capture Data [%ls] doesn't contain valid Timecode or Frame Rate information", *InCaptureData->GetName());
		return OrderedArray;
	}

	int64 FootageRecordingTimestamp = Timecode.ToTimespan(FrameRate).GetTicks();

	TArray<TPair<int64, UCameraCalibration*>> OrderedMap;

	for (UCameraCalibration* CameraCalibration : InCameraCalibrations)
	{
		if (!CameraCalibration)
		{
			// Nothing to log
			continue;
		}

		UCameraCalibrationMetadata* Metadata = UCameraCalibrationMetadata::GetCameraCalibrationMetadata(CameraCalibration);

		if (!Metadata)
		{
			UE_LOGF(LogTimecodeBasedCalibrationSelector, Warning, "Ignoring calibration asset [%ls] due to missing metadata.", *CameraCalibration->GetName());
			continue;
		}

		/* 
		 * Calibrations with a high reprojection RMS error(>1.0 pixel) indicate poor checkerboard detection or lens model fit.
		 * A zero value means the calibration tool failed to compute the metric at all. Either case produces unreliable intrinsics/distortion, so we are skipping it.
		 */
		static constexpr double MaxReprojectionRMSError = 1.0;
		if (FMath::IsNearlyZero(Metadata->ReprojectionRMSError) || Metadata->ReprojectionRMSError > MaxReprojectionRMSError)
		{
			UE_LOGF(LogTimecodeBasedCalibrationSelector, Warning, "Ignoring calibration asset [%ls] due to a high reprojection RMS error (%lf).", 
					*CameraCalibration->GetName(), Metadata->ReprojectionRMSError);
			continue;
		}

		/* 
		 * A calibration computed from fewer than 5 frames lacks sufficient coverage of the lens distortion space, leading to unstable or overfitted intrinsics. 
		 * The solver needs views from diverse angles to constrain all distortion coefficients reliably, so we are skipping it. 
		 */
		static constexpr int32 MinNumberOfFrames = 5;
		if (Metadata->SelectedFrames.Num() < MinNumberOfFrames)
		{
			UE_LOGF(LogTimecodeBasedCalibrationSelector, Warning, "Ignoring calibration asset [%ls] due to a low number of frames (%d) used to generate calibration.", 
					*CameraCalibration->GetName(), Metadata->SelectedFrames.Num());
			continue;
		}

		if (!Metadata->GenerationTimecode.IsValid() || !Metadata->GenerationFrameRate.IsValid() || Metadata->GenerationFrameRate.Numerator == 0)
		{
			UE_LOGF(LogTimecodeBasedCalibrationSelector, Warning, "Ignoring calibration asset [%ls] due to invalid timecode (%ls) and frame rate information.", 
					*CameraCalibration->GetName(), *Metadata->GenerationTimecode.ToString());
			continue;
		}

		int64 CalibrationRecordingTimestamp = Metadata->GenerationTimecode.ToTimespan(Metadata->GenerationFrameRate).GetTicks();

		int64 Diff = FMath::Abs(FootageRecordingTimestamp - CalibrationRecordingTimestamp);

		OrderedMap.Add({ Diff, CameraCalibration });
	}

	if (!OrderedMap.IsEmpty())
	{
		OrderedMap.Sort([](const TPair<int64, UCameraCalibration*>& InLeft, const TPair<int64, UCameraCalibration*>& InRight)
						{
							return TLess<int64>()(InLeft.Key, InRight.Key);
						});

		Algo::Transform(OrderedMap, OrderedArray, [](const TPair<int64, UCameraCalibration*>& InElem)
						{
							return InElem.Value;
						});

		UCameraCalibrationMetadata* Metadata = UCameraCalibrationMetadata::GetCameraCalibrationMetadata(OrderedArray[0]);
		check(Metadata);

		UE_LOGF(LogTimecodeBasedCalibrationSelector, Display,
				"Selected camera calibration [%ls] for Footage Capture Data [%ls] based on closest timecode match: footage [%ls] vs calibration [%ls].",
				*OrderedArray[0]->GetName(),
				*InCaptureData->GetName(),
				*Timecode.ToString(),
				*Metadata->GenerationTimecode.ToString()
				);
	}
	else
	{
		UE_LOGF(LogTimecodeBasedCalibrationSelector, Error, "Failed to find calibration assets for Footage Capture Data [%ls]", *InCaptureData->GetName());
	}

	return OrderedArray;
}

TSubclassOf<UMetaHumanCalibrationSelectorSettings> UMetaHumanTimecodeBasedSelector::GetSettingsClass_Implementation() const
{
	return UMetaHumanCalibrationSelectorSettings::StaticClass();
}
