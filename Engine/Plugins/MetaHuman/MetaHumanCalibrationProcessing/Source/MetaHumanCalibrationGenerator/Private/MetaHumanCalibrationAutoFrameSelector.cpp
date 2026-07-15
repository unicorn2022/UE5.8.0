// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCalibrationAutoFrameSelector.h"

#include "MetaHumanCalibrationPatternDetector.h"
#include "Utils/MetaHumanCalibrationAutoFrameSelection.h"
#include "Utils/MetaHumanCalibrationFrameResolver.h"

#include "ImageSequenceUtils.h"
#include "ImgMediaSource.h"

#include "Settings/MetaHumanCalibrationGeneratorSettings.h"

#include "Utils/MetaHumanCalibrationUtils.h"

#include "Misc/ScopedSlowTask.h"
#include "Tasks/Task.h"
#include "Async/Fundamental/Task.h"
#include "Async/Monitor.h"

#define LOCTEXT_NAMESPACE "UMetaHumanCalibrationAutoFrameSelector"

DEFINE_LOG_CATEGORY_STATIC(LogMetaHumanCalibrationAutoFrameSelector, Log, All);

TArray<int32> UMetaHumanCalibrationAutoFrameSelector::Run(const UFootageCaptureData* InCaptureData,
														  const UMetaHumanCalibrationGeneratorConfig* InConfig,
														  const UMetaHumanCalibrationGeneratorOptions* InOptions)
{
	using namespace UE::MetaHuman::Image;

	if (!IsValid(InCaptureData))
	{
		UE_LOGF(LogMetaHumanCalibrationAutoFrameSelector, Error, "Invalid Capture Data asset provided");
		return TArray<int32>();
	}

	if (!IsValid(InConfig))
	{
		UE_LOGF(LogMetaHumanCalibrationAutoFrameSelector, Error, "Invalid Config object provided");
		return TArray<int32>();
	}

	TValueOrError<void, FString> ConfigValid = InConfig->CheckConfigValidity();
	if (ConfigValid.HasError())
	{
		UE_LOGF(LogMetaHumanCalibrationAutoFrameSelector, Error, "Config doesn't pass validity check: %ls", *ConfigValid.GetError());
		return TArray<int32>();
	}

	if (!IsValid(InOptions))
	{
		UE_LOGF(LogMetaHumanCalibrationAutoFrameSelector, Error, "Invalid Options object provided");
		return TArray<int32>();
	}

	if (InCaptureData->ImageSequences.Num() < 2)
	{
		UE_LOGF(LogMetaHumanCalibrationAutoFrameSelector, Error, "Stereo calibration process expects 2 cameras, but found %d", InCaptureData->ImageSequences.Num());
		return TArray<int32>();
	}

	FMetaHumanCalibrationPatternDetector::FPatternInfo PatternInfo = {
		.Width = InConfig->BoardPatternWidth - 1,
		.Height = InConfig->BoardPatternHeight - 1,
		.SquareSize = InConfig->BoardSquareSize
	};

	TArray<FMetaHumanCalibrationPatternDetector::FCameraInfo> Cameras;
	for (const UImgMediaSource* ImageSource : InCaptureData->ImageSequences)
	{
		if (!IsValid(ImageSource))
		{
			UE_LOGF(LogMetaHumanCalibrationAutoFrameSelector, Error, "Invalid Image Media Source asset provided");
			return TArray<int32>();
		}

		FIntVector2 ImageDimensions;
		int32 NumberOfImages = 0;
		FImageSequenceUtils::GetImageSequenceInfoFromAsset(ImageSource, ImageDimensions, NumberOfImages);

		FMetaHumanCalibrationPatternDetector::FCameraInfo CameraInfo =
		{
			.Name = ImageSource->GetName(),
			.Dimensions = ImageDimensions
		};

		Cameras.Add(MoveTemp(CameraInfo));
	}
	
	const UMetaHumanCalibrationGeneratorSettings* Settings = GetDefault<UMetaHumanCalibrationGeneratorSettings>();

	TUniquePtr<FMetaHumanCalibrationPatternDetector> PatternDetector =
		FMetaHumanCalibrationPatternDetector::CreateFromNew(PatternInfo, Cameras, Settings->ScaleFactor);

	using FFrameCameraPaths = TPair<TArray<FString>, TArray<FString>>;
	FFrameCameraPaths CameraPaths;

	TOptional<FMetaHumanCalibrationFrameResolver> ResolverOpt = FMetaHumanCalibrationFrameResolver::CreateFromCaptureData(InCaptureData);
	if (!ResolverOpt.IsSet())
	{
		UE_LOGF(LogMetaHumanCalibrationAutoFrameSelector, Error, "Frame Resolver is NOT valid.");
		return TArray<int32>();
	}

	FMetaHumanCalibrationFrameResolver Resolver = MoveTemp(ResolverOpt.GetValue());

	if (!Resolver.HasFrames())
	{
		UE_LOGF(LogMetaHumanCalibrationAutoFrameSelector, Error, "No matching frames found.");
		return TArray<int32>();
	}

	Resolver.GetFramePathsForCameraIndex(0, CameraPaths.Key);
	Resolver.GetFramePathsForCameraIndex(1, CameraPaths.Value);

	TArray<int32> FilteredFrameIndices;

	TPair<TArray<FString>, TArray<FString>> FilteredFramePaths =
		FilterFramePaths(CameraPaths, [&FilteredFrameIndices, SampleRate = Settings->AutomaticFrameSelectionSampleRate](int32 InFrame)
	{
		if (InFrame % SampleRate == 0)
		{
			FilteredFrameIndices.Add(InFrame);
			return true;
		}

		return false;
	});

	TPair<FString, FString> CameraPair;
	CameraPair.Key = Cameras[0].Name;
	CameraPair.Value = Cameras[1].Name;

	TPair<FIntVector2, FIntVector2> DimensionsPair;
	DimensionsPair.Key = Cameras[0].Dimensions;
	DimensionsPair.Value = Cameras[1].Dimensions;

	FScopedSlowTask AutoFrameSelectionTask(FilteredFrameIndices.Num(), LOCTEXT("AutoFrameSelection_TaskMessage", "Running automatic frame selection process"));
	AutoFrameSelectionTask.MakeDialog(true);

	using FDetectedFrame = FMetaHumanCalibrationPatternDetector::FDetectedFrame;
	using FDetectedFrames = FMetaHumanCalibrationPatternDetector::FDetectedFrames;
	using FFramePaths = FMetaHumanCalibrationPatternDetector::FFramePaths;

	using namespace UE::CaptureManager;

	// FilteredFrameIndices is accessed from multiple threads so we are protecting it using TMonitor
	TMonitor<TArray<int32>> ScopedFrameIndicesMonitor(MoveTemp(FilteredFrameIndices));

	auto FailureFrameProviderLambda =
		[&CameraPaths, FilteredFramePaths, &ScopedFrameIndicesMonitor](const FFramePaths& InFailedPaths, int32 InTry) -> TOptional<FFramePaths>
		{
			int32 FirstCameraPathIndex = CameraPaths.Key.IndexOfByKey(InFailedPaths.Key);
			int32 SecondCameraPathIndex = CameraPaths.Value.IndexOfByKey(InFailedPaths.Value);

			check(FirstCameraPathIndex == SecondCameraPathIndex);

			int32 Index = FirstCameraPathIndex;

			if (Index == INDEX_NONE)
			{
				return {};
			}

			const int32 PathIndex = Index + InTry;

			FString NewFirstCameraPath;
			FString NewSecondCameraPath;

			if (CameraPaths.Key.IsValidIndex(PathIndex) && CameraPaths.Value.IsValidIndex(PathIndex))
			{
				NewFirstCameraPath = CameraPaths.Key[PathIndex];
				NewSecondCameraPath = CameraPaths.Value[PathIndex];
			}

			if (NewFirstCameraPath.IsEmpty() || NewSecondCameraPath.IsEmpty())
			{
				return {};
			}

			if (FilteredFramePaths.Key.Contains(NewFirstCameraPath) ||
				FilteredFramePaths.Value.Contains(NewSecondCameraPath))
			{
				return {};
			}

			{
				TMonitor<TArray<int32>>::FHelper ScopeSelectedLock = ScopedFrameIndicesMonitor.Lock();

				int32 IndexToFind = FirstCameraPathIndex + InTry - 1;

				int32 IndexOfChanged = ScopeSelectedLock->Find(IndexToFind);
				ScopeSelectedLock->operator[](IndexOfChanged) = PathIndex;
			}

			FFramePaths NewFrame = { MoveTemp(NewFirstCameraPath), MoveTemp(NewSecondCameraPath) };
			return NewFrame;
		};

	std::atomic_int Progress = 0;
	auto ProgressReporter = [&Progress](double)
		{
			++Progress;
		};

	UE::CaptureManager::FStopRequester StopRequest;
	static constexpr int32 NumberOfThreads = 8;
	PatternDetector->SetMaximumNumberOfThreads(NumberOfThreads);
	PatternDetector->SetOnProgressReporter(FMetaHumanCalibrationPatternDetector::FProgressReporter::CreateLambda(MoveTemp(ProgressReporter)));
	PatternDetector->SetStopToken(StopRequest.CreateToken());

	TPromise<FDetectedFrames> Promise;
	TFuture<FDetectedFrames> Future = Promise.GetFuture();
	auto Task = [this,
		InOptions,
		CameraNames = CameraPair,
		FilteredFramePaths = MoveTemp(FilteredFramePaths),
		FailureFrameProviderLambda = MoveTemp(FailureFrameProviderLambda),
		PatternDetector = MoveTemp(PatternDetector),
		&Promise]() mutable
		{
			FDetectedFrames DetectedFramesFromPattern =
				PatternDetector->DetectPatterns(CameraNames,
												FilteredFramePaths,
												InOptions->SharpnessThreshold,
												FMetaHumanCalibrationPatternDetector::FOnFailureFrameProvider::CreateLambda(MoveTemp(FailureFrameProviderLambda)));

			Promise.SetValue(MoveTemp(DetectedFramesFromPattern));
		};

	using namespace UE::Tasks;
	Launch(UE_SOURCE_LOCATION, MoveTemp(Task), ETaskPriority::BackgroundNormal);

	while (!Future.WaitFor(FTimespan::FromMilliseconds(50)))
	{
		if (AutoFrameSelectionTask.ShouldCancel())
		{
			StopRequest.RequestStop();
			break;
		}

		int32 CurrentProgress = Progress.exchange(0);
		AutoFrameSelectionTask.EnterProgressFrame(CurrentProgress);
	}

	FDetectedFrames DetectedFramesFromPattern = Future.Get();

	// In case of leftovers
	AutoFrameSelectionTask.EnterProgressFrame(Progress.load());

	if (StopRequest.IsStopRequested())
	{
		return TArray<int32>();
	}

	// Grabbing the frame indices back from protected array
	FilteredFrameIndices = ScopedFrameIndicesMonitor.Claim();

	TMap<int32, FDetectedFrame> DetectedFrames;
	for (const auto& [Index, DetectedFrame] : DetectedFramesFromPattern)
	{
		DetectedFrames.Add(FilteredFrameIndices[Index], DetectedFrame);
	}

	TPair<FBox2D, FBox2D> AreaOfInterestPair;
	AreaOfInterestPair.Key = FBox2D(FVector2D::ZeroVector, FVector2D(DimensionsPair.Key));
	AreaOfInterestPair.Value = FBox2D(FVector2D::ZeroVector, FVector2D(DimensionsPair.Value));

	if (InOptions->AreaOfInterestsForCameras.Num() > 2)
	{
		AreaOfInterestPair.Key = InOptions->AreaOfInterestsForCameras[0].GetBox2D();
		AreaOfInterestPair.Value = InOptions->AreaOfInterestsForCameras[1].GetBox2D();
	}

	FMetaHumanCalibrationAutoFrameSelection FrameSelector(MoveTemp(CameraPair), MoveTemp(DimensionsPair), MoveTemp(AreaOfInterestPair));

	return FrameSelector.RunSelection(PatternInfo, DetectedFrames);
}

#undef LOCTEXT_NAMESPACE