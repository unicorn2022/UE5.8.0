// Copyright Epic Games, Inc. All Rights Reserved.

#include "UMetaHumanRobustFeatureMatcher.h"

#include "ImageSequenceUtils.h"
#include "ImgMediaSource.h"

#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

#include "Misc/FileHelper.h"

#include "CaptureMetadata.h"
#include "Utils/MetaHumanCalibrationUtils.h"
#include "Utils/MetaHumanCalibrationFrameResolver.h"
#include "UObject/Package.h"

#include "TakeDirectoryUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogMetaHumanRobustFeatureMatcher, Log, All);

namespace Private
{

static TOptional<FCameraCalibration> GetCalibrationForView(const TArray<FCameraCalibration>& InCameraCalibrations,
														   TObjectPtr<UImgMediaSource> InView,
														   int32 InViewIndex)
{
	check(InView);

	UPackage* Package = InView->GetPackage();
	check(Package);

	FMetaData& MetaData = Package->GetMetaData();

	FName CameraIdTag = GET_MEMBER_NAME_CHECKED(UCaptureMetadata, CameraId);
	if (MetaData.HasValue(InView, CameraIdTag))
	{
		FString CameraId = MetaData.GetValue(InView, CameraIdTag);

		const FCameraCalibration* Found =
			InCameraCalibrations.FindByPredicate([CameraId](const FCameraCalibration& InCameraCalibration)
			{
				return InCameraCalibration.CameraId == CameraId;
			});

		if (Found)
		{
			return *Found;
		}
	}

	if (InCameraCalibrations.IsValidIndex(InViewIndex))
	{
		return InCameraCalibrations[InViewIndex];
	}

	return {};
}

}

bool FDetectedFeatures::IsValid() const
{
	if (FrameIndex == INDEX_NONE || Points3d.IsEmpty() || CameraPoints.IsEmpty() || Points3dReprojected.IsEmpty())
	{
		return false;
	}

	return true;
}

UMetaHumanRobustFeatureMatcher::UMetaHumanRobustFeatureMatcher()
	: FeatureMatcher(MakeShared<UE::Wrappers::FMetaHumanRobustFeatureMatcher>())
{
}

bool UMetaHumanRobustFeatureMatcher::Init(UFootageCaptureData* InCaptureData, UMetaHumanCalibrationDiagnosticsOptions* InOptions)
{
	if (!InOptions->CameraCalibration)
	{
		UE_LOGF(LogMetaHumanRobustFeatureMatcher, Error, "Missing Camera Calibration asset");
		return false;
	}

	if (InCaptureData->ImageSequences.Num() < 2)
	{
		UE_LOGF(LogMetaHumanRobustFeatureMatcher, Error, "Feature matching process expects two cameras but found %d", InCaptureData->ImageSequences.Num());
		return false;
	}

	if (!(IsValid(InCaptureData->ImageSequences[0]) && IsValid(InCaptureData->ImageSequences[1])))
	{
		UE_LOGF(LogMetaHumanRobustFeatureMatcher, Error, "Image sequences are invalid");
		return false;
	}

	TArray<FCameraCalibration> Calibrations;
	TArray<TPair<FString, FString>> StereoReconstructionPairs;
	InOptions->CameraCalibration->ConvertToTrackerNodeCameraModels(Calibrations, StereoReconstructionPairs);

	TOptional<FMetaHumanCalibrationFrameResolver> ResolverOpt = FMetaHumanCalibrationFrameResolver::CreateFromCaptureData(InCaptureData);
	check(ResolverOpt.IsSet());

	FMetaHumanCalibrationFrameResolver Resolver = MoveTemp(ResolverOpt.GetValue());
	if (!Resolver.HasFrames())
	{
		UE_LOGF(LogMetaHumanRobustFeatureMatcher, Error, "No matching frames found.");
		return false;
	}

	
	static constexpr int32 ExpectedNumberOfCameras = 2;
	TArray<FCameraCalibration> MatchedCalibrations;
	MatchedCalibrations.Reserve(ExpectedNumberOfCameras);

	for (int32 Index = 0; Index < ExpectedNumberOfCameras; ++Index)
	{
		UImgMediaSource* View = InCaptureData->ImageSequences[Index];

		FString CameraId = UE::MetaHuman::Image::GetCameraId(View);

		TArray<FString>& ImagePath = StereoPairImagePaths.Add(CameraId);
		Resolver.GetFramePathsForCameraIndex(Index, ImagePath);

		TOptional<FCameraCalibration> CameraCalibrationOpt = Private::GetCalibrationForView(Calibrations, View, Index);

		if (CameraCalibrationOpt.IsSet())
		{
			FCameraCalibration CameraCalibration = MoveTemp(CameraCalibrationOpt.GetValue());
			StereoPairImageSizes.Add(CameraId, FIntVector2(CameraCalibration.ImageSize.X, CameraCalibration.ImageSize.Y));

			MatchedCalibrations.Add(MoveTemp(CameraCalibration));
		}
		else
		{
			UE_LOGF(LogMetaHumanRobustFeatureMatcher, Error, "Could not determine calibration for the camera %ls.", *View->GetName());
			return false;
		}
	}

	if (MatchedCalibrations.Num() < ExpectedNumberOfCameras)
	{
		UE_LOGF(LogMetaHumanRobustFeatureMatcher, Error, "Feature matching process expects 2 cameras with calibration but found %d", MatchedCalibrations.Num());
		return false;
	}

	bool bSuccess = FeatureMatcher->Init(MatchedCalibrations, InOptions->FeatureMatchErrorThreshold, InOptions->RatioThreshold);
	if (!bSuccess)
	{
		UE_LOGF(LogMetaHumanRobustFeatureMatcher, Error, "Failed to initialize the feature matching process.");
		return false;
	};

	for (const FCameraCalibration& CameraCalibration : MatchedCalibrations)
	{
		FeatureMatcher->AddCamera(CameraCalibration.CameraId, CameraCalibration.ImageSize.X, CameraCalibration.ImageSize.Y);
	}

	return true;
}

bool UMetaHumanRobustFeatureMatcher::DetectFeatures(int64 InFrame)
{
	if (StereoPairImagePaths.IsEmpty())
	{
		UE_LOGF(LogMetaHumanRobustFeatureMatcher, Error, "Feature matcher isn't initialized");
		return false;
	}

	TArray<TArray64<uint8>> ArrayImageData;
	for (const TPair<FString, TArray<FString>>& CameraFrames : StereoPairImagePaths)
	{
		const TArray<FString>& CameraFramePaths = CameraFrames.Value;
		if (!CameraFrames.Value.IsValidIndex(InFrame))
		{
			UE_LOGF(LogMetaHumanRobustFeatureMatcher, Error, "Camera %ls doesn't contain the frame %lld.", *CameraFrames.Key, InFrame);
			return false;
		}

		TArray64<uint8> CameraImage = UE::MetaHuman::Image::GetGrayscaleImageData(CameraFramePaths[InFrame]);

		if (CameraImage.IsEmpty())
		{
			UE_LOGF(LogMetaHumanRobustFeatureMatcher, Error, "Image %lld for camera %ls couldn't be read.", InFrame, *CameraFrames.Key);
			return false;
		}

		ArrayImageData.Add(CameraImage);
	}
	
	TArray<const unsigned char*> ImageData;
	for (const TArray64<uint8>& ArrayImage : ArrayImageData)
	{
		ImageData.Add(ArrayImage.GetData());
	}

	return FeatureMatcher->DetectFeatures(InFrame, ImageData);
}

FDetectedFeatures UMetaHumanRobustFeatureMatcher::GetFeatures(int64 InFrame)
{
	TArray<FVector> OutPoints3d;
	TArray<TArray<FVector2D>> OutCameraPoints;
	TArray<TArray<FVector2D>> OutPoints3dReprojected;

	FDetectedFeatures DetectedFeatures;
	bool bSuccess = FeatureMatcher->GetFeatures(InFrame, OutPoints3d, OutCameraPoints, OutPoints3dReprojected);
	if (!bSuccess)
	{
		return DetectedFeatures;
	}

	DetectedFeatures.FrameIndex = InFrame;
	DetectedFeatures.Points3d = MoveTemp(OutPoints3d);

	Algo::Transform(OutCameraPoints, DetectedFeatures.CameraPoints, [](const TArray<FVector2D>& InElem)
					{
						FCameraPoints CameraPoints;
						CameraPoints.Points = InElem;
						return CameraPoints;
					});

	Algo::Transform(OutPoints3dReprojected, DetectedFeatures.Points3dReprojected, [](const TArray<FVector2D>& InElem)
					{
						FCameraPoints CameraPoints;
						CameraPoints.Points = InElem;
						return CameraPoints;
					});

	return DetectedFeatures;
}

TArray<FString> UMetaHumanRobustFeatureMatcher::GetImagePaths(const FString& InCameraName)
{
	if (TArray<FString>* Found = StereoPairImagePaths.Find(InCameraName))
	{
		return *Found;
	}

	return TArray<FString>();
}

TArray<FString> UMetaHumanRobustFeatureMatcher::GetCameraNames() const
{
	TArray<FString> CameraNames;
	StereoPairImagePaths.GenerateKeyArray(CameraNames);

	return CameraNames;
}

TArray<FIntVector2> UMetaHumanRobustFeatureMatcher::GetImageSizes() const
{
	TArray<FIntVector2> ImageSizes;

	TArray<FString> CameraNames = GetCameraNames();
	for (const FString& CameraName : CameraNames)
	{
		const FIntVector2* Found = StereoPairImageSizes.Find(CameraName);
		if (ensure(Found))
		{
			ImageSizes.Add(*Found);
		}
	}

	return ImageSizes;
}