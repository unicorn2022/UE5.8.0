// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selectors/MetaHumanDiagnosticsBasedSelector.h"

#include "CalibrationArraySplitter.h"

#include "Utils/MetaHumanCalibrationErrorCalculator.h"
#include "Utils/MetaHumanCalibrationErrorAnalysis.h"

#include "CaptureData.h"
#include "CameraCalibration.h"

#include "UMetaHumanRobustFeatureMatcher.h"

#include "Async/Monitor.h"
#include "Misc/ScopedSlowTask.h"

#include "Templates/Greater.h"

#include "Async/ParallelFor.h"
#include "Async/StopToken.h"

#include "Async/TaskGraphInterfaces.h"

DEFINE_LOG_CATEGORY_STATIC(LogDiagnosticsBasedCalibrationSelector, Log, All)

TArray<UCameraCalibration*> UMetaHumanDiagnosticsBasedSelector::OrderCalibrations_Implementation(UFootageCaptureData* InCaptureData, const TArray<UCameraCalibration*>& InCameraCalibrations) const
{
	TArray<UCameraCalibration*> OrderedCameraCalibrations;

	if (!InCaptureData)
	{
		UE_LOGF(LogDiagnosticsBasedCalibrationSelector, Error, "Footage Capture Data isn't provided");
		return OrderedCameraCalibrations;
	}

	if (InCaptureData->ImageSequences.Num() < 2)
	{
		UE_LOGF(LogDiagnosticsBasedCalibrationSelector, Error, "Diagnostics based selector requires 2 image sequences in the Footage Capture Data [%ls], but found %d", 
				*InCaptureData->GetName(),
				InCaptureData->ImageSequences.Num());
		return OrderedCameraCalibrations;
	}

	if (!InCaptureData->ImageSequences[0] || !InCaptureData->ImageSequences[1])
	{
		UE_LOGF(LogDiagnosticsBasedCalibrationSelector, Error, "Image sequences can't be null in the Footage Capture Data [%ls]", *InCaptureData->GetName());
		return OrderedCameraCalibrations;
	}

	const UMetaHumanDiagnosticsBasedSelectorSettings* LocalSettings = GetSettings<UMetaHumanDiagnosticsBasedSelectorSettings>();
	if (!LocalSettings || !LocalSettings->FrameProvider)
	{
		UE_LOGF(LogDiagnosticsBasedCalibrationSelector, Error, "Diagnostics based selector requires a valid settings object");
		return OrderedCameraCalibrations;
	}

	const TArray<int32>& SelectedFrames = LocalSettings->FrameProvider->GetSelectedFrames();

	if (SelectedFrames.IsEmpty())
	{
		UE_LOGF(LogDiagnosticsBasedCalibrationSelector, Error, "Diagnostics based selector requires frame selection to run the diagnostics on");
		return OrderedCameraCalibrations;
	}

	FScopedSlowTask SlowTask(InCameraCalibrations.Num() * SelectedFrames.Num(), NSLOCTEXT("MetaHumanDiagnosticsBasedSelector", "MetaHumanDiagnosticsBasedSelector_Dialog", "Running calibration selection..."));
	SlowTask.MakeDialog(true);

	using FOrderedArray = TArray<TPair<double, UCameraCalibration*>>;
	
	// ProtectedOrderedMap variable is used as a thread-safe container
	UE::CaptureManager::TMonitor<FOrderedArray> ProtectedOrderedMap;
	UE::CaptureManager::FStopRequester StopRequest;
	std::atomic_int Progress = 0;

	int32 NumWorkers = FTaskGraphInterface::Get().GetNumWorkerThreads();

	const int32 NumberOfThreads = NumWorkers < MaxNumberOfThreads ? NumWorkers : MaxNumberOfThreads;

	TArray<TArrayView<UCameraCalibration* const>> SplitArray = UE::MetaHuman::SplitCalibrationArrayToViews(InCameraCalibrations, NumberOfThreads);

	TArray<UE::Tasks::FTask> RunningTasks;
	RunningTasks.Reserve(SplitArray.Num());

	for (TArrayView<UCameraCalibration* const> Chunk : SplitArray)
	{
		auto Task = [Chunk, InCaptureData, SelectedFrames, &ProtectedOrderedMap, StopToken = StopRequest.CreateToken(), &Progress]()
			{
				if (StopToken.IsStopRequested())
				{
					return;
				}

				for (UCameraCalibration* CameraCalibration : Chunk)
				{
					TStrongObjectPtr<UMetaHumanCalibrationDiagnosticsOptions> DiagnosticsOptions(NewObject<UMetaHumanCalibrationDiagnosticsOptions>());
					DiagnosticsOptions->CameraCalibration = CameraCalibration;

					TStrongObjectPtr<UMetaHumanRobustFeatureMatcher> RobustFeatureMatcher(NewObject<UMetaHumanRobustFeatureMatcher>());
					RobustFeatureMatcher->Init(InCaptureData, DiagnosticsOptions.Get());

					static const FVector2D GridSize(8.0, 8.0);
					FMetaHumanCalibrationErrorCalculator Calculator(GridSize, RobustFeatureMatcher->GetCameraNames(), RobustFeatureMatcher->GetImageSizes());

					TArray<int32> SuccessfullyDetectedFrames;

					for (int32 FrameNumber : SelectedFrames)
					{
						if (StopToken.IsStopRequested())
						{
							return;
						}

						bool bSuccess = RobustFeatureMatcher->DetectFeatures(FrameNumber);
						if (bSuccess)
						{
							FDetectedFeatures DetectedFeatures =
								RobustFeatureMatcher->GetFeatures(FrameNumber);

							Calculator.Update(DetectedFeatures);

							SuccessfullyDetectedFrames.Add(FrameNumber);
						}

						++Progress;
					}

					// Ignore calibrations for which we failed to detect features
					if (SuccessfullyDetectedFrames.IsEmpty())
					{
						UE_LOGF(LogDiagnosticsBasedCalibrationSelector, Error, "Ignoring calibration asset [%ls] due to failure to detect features on the Footage Capture Data [%ls]", *CameraCalibration->GetName(), *InCaptureData->GetName());
						continue;
					}

					FMetaHumanCalibrationErrorAnalysis Analysis(Calculator, SuccessfullyDetectedFrames);
					TMap<FString, FCameraCalibrationScore> CalibrationScores = Analysis.Analyze();

					TArray<FCameraCalibrationScore> Scores;
					CalibrationScores.GenerateValueArray(Scores);

					double TotalScore = 0.0;
					for (const FCameraCalibrationScore& Score : Scores)
					{
						TotalScore += Score.MeanTotalScore;
					}

					TotalScore /= Scores.Num();

					ProtectedOrderedMap->Add({ TotalScore, CameraCalibration });

					RobustFeatureMatcher->MarkAsGarbage();
					DiagnosticsOptions->MarkAsGarbage();
				}
			};

		RunningTasks.Add(UE::Tasks::Launch(UE_SOURCE_LOCATION, MoveTemp(Task), UE::Tasks::ETaskPriority::BackgroundNormal));
	}

	UE::Tasks::FTask WaiterTask = 
		UE::Tasks::Launch(UE_SOURCE_LOCATION, [] {}, RunningTasks, UE::Tasks::ETaskPriority::BackgroundNormal);

	while (!WaiterTask.IsCompleted())
	{
		if (SlowTask.ShouldCancel())
		{
			StopRequest.RequestStop();
			WaiterTask.Wait(); // Wait for all tasks to complete before break
			break;
		}

		int32 CurrentProgress = Progress.exchange(0);
		SlowTask.EnterProgressFrame(CurrentProgress);

		// Sleeping instead of calling a wait with timespan as Wait may try to execute tasks in this thread which defeats the purpose of SlowTask dialog
		FPlatformProcess::SleepNoStats(0.05f);
	}

	RunningTasks.Reset();

	// In case of leftovers
	SlowTask.EnterProgressFrame(Progress.load());

	if (StopRequest.IsStopRequested())
	{
		return OrderedCameraCalibrations;
	}

	FOrderedArray OrderedMap = ProtectedOrderedMap.Claim();

	if (!OrderedMap.IsEmpty())
	{
		OrderedMap.Sort([](const TPair<double, UCameraCalibration*>& InLeft, const TPair<double, UCameraCalibration*>& InRight)
						{
							return TGreater<double>()(InLeft.Key, InRight.Key);
						});

		Algo::Transform(OrderedMap, OrderedCameraCalibrations, [](const TPair<double, UCameraCalibration*>& InElem)
						{
							return InElem.Value;
						});

		FOrderedArray::TConstIterator Iterator = OrderedMap.CreateConstIterator();
		UE_LOGF(LogDiagnosticsBasedCalibrationSelector, Display,
			   "Selected camera calibration [%ls] for Footage Capture Data [%ls] based on best feature-matching diagnostic score (%lf)", 
				*Iterator->Value->GetName(),
				*InCaptureData->GetName(),
				Iterator->Key)
	}
	else
	{
		UE_LOGF(LogDiagnosticsBasedCalibrationSelector, Error, "Failed to find calibration assets for Footage Capture Data [%ls]", *InCaptureData->GetName());
	}

	return OrderedCameraCalibrations;
}

TSubclassOf<UMetaHumanCalibrationSelectorSettings> UMetaHumanDiagnosticsBasedSelector::GetSettingsClass_Implementation() const
{
	return UMetaHumanDiagnosticsBasedSelectorSettings::StaticClass();
}
