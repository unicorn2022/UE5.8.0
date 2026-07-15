// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCalibrationGenerator.h"

#include "ImageSequenceUtils.h"
#include "ImgMediaSource.h"

#include "Async/Async.h"
#include "Async/Monitor.h"
#include "Async/ParallelFor.h"

#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"

#include "Utils/MetaHumanCalibrationNotificationManager.h"
#include "Utils/MetaHumanCalibrationUtils.h"
#include "Utils/MetaHumanCalibrationFrameResolver.h"

#include "MetaHumanCalibrationPatternDetector.h"

#include "Metadata/MetadataHandler.h"
#include "CaptureMetadata.h"
#include "CameraCalibrationMetadata.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanCalibrationGenerator)

#define LOCTEXT_NAMESPACE "MetaHumanCalibrationGenerator"

DEFINE_LOG_CATEGORY_STATIC(LogMetaHumanCalibrationGenerator, Log, All);

namespace UE::MetaHuman::Private
{

static UCameraCalibration* CreateCameraCalibrationAsset(const FString& InTargetPackagePath, const FString& InDesiredAssetName)
{
	IAssetRegistry& AssetRegistry = FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();

	FString AssetName = InDesiredAssetName;
	FString ObjectPathToCheck = InTargetPackagePath / (AssetName + FString(TEXT(".")) + AssetName);
	FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(ObjectPathToCheck));

	int32 Counter = 1;
	while (AssetData.IsValid())
	{
		AssetName = InDesiredAssetName + TEXT("_") + FString::FromInt(Counter++);
		ObjectPathToCheck = InTargetPackagePath / (AssetName + FString(TEXT(".")) + AssetName);
		AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(ObjectPathToCheck));
	}

	return Cast<UCameraCalibration>(AssetTools.CreateAsset(AssetName, InTargetPackagePath, UCameraCalibration::StaticClass(), nullptr));
}

static void SaveCalibrationProcessCreatedAssets(const FString& InAssetPath)
{
	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	IAssetRegistry& AssetRegistry = FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetData> AssetsData;
	AssetRegistry.GetAssetsByPath(FName{ *InAssetPath }, AssetsData, true, false);

	if (AssetsData.IsEmpty())
	{
		return;
	}

	TArray<UPackage*> Packages;
	for (const FAssetData& AssetData : AssetsData)
	{
		UPackage* Package = AssetData.GetAsset()->GetPackage();
		if (!Packages.Contains(Package))
		{
			Packages.Add(Package);
		}
	}

	UEditorLoadingAndSavingUtils::SavePackages(Packages, true);
}

static TArray<FCameraCalibration> MatchImageSequenceWithCalibration(const TArray<UImgMediaSource*> InImageSequences,
																	const TArray<FCameraCalibration>& InCameraCalibrations)
{
	TArray<FCameraCalibration> MatchedCameraCalibrations;

	for (UImgMediaSource* ImgMediaSource : InImageSequences)
	{
		FString CameraName = UE::MetaHuman::Image::GetCameraId(ImgMediaSource);

		const FCameraCalibration* Match = InCameraCalibrations.FindByPredicate([CameraName](const FCameraCalibration& InCameraCalibration)
		{
			return CameraName == InCameraCalibration.CameraId;
		});

		check(Match);

		if (Match)
		{
			MatchedCameraCalibrations.Add(*Match);
		}
	}

	return MatchedCameraCalibrations;
}

static void AddMetadataInformation(TStrongObjectPtr<const UMetaHumanCalibrationGenerator> InOwner,
								   TStrongObjectPtr<const UMetaHumanCalibrationGeneratorOptions> InOptions,
								   TStrongObjectPtr<const UFootageCaptureData> InCaptureData,
								   TObjectPtr<UCameraCalibration> OutCameraCalibration)
{
	UCameraCalibrationMetadata* Metadata = NewObject<UCameraCalibrationMetadata>();

	Metadata->ReprojectionRMSError = InOwner->GetLastRMSError();

	check(!InCaptureData->ImageSequences.IsEmpty());
	check(InCaptureData->ImageSequences[0]);

	TObjectPtr<UImgMediaSource> ImageMediaSource = InCaptureData->ImageSequences[0];

	Metadata->GenerationTimecode = ImageMediaSource->StartTimecode;
	Metadata->GenerationFrameRate = ImageMediaSource->FrameRateOverride;
	Metadata->SelectedFrames = InOptions->SelectedFrames;

	UE::SetMetadataObject<UCameraCalibrationMetadata>(OutCameraCalibration, Metadata);
}

static void CreateCalibrationAsset_GameThread(TStrongObjectPtr<const UMetaHumanCalibrationGenerator> InOwner,
											  TStrongObjectPtr<const UMetaHumanCalibrationGeneratorOptions> InOptions,
											  TArray<FCameraCalibration> InCameraCalibrations,
											  TSharedPtr<FMetaHumanCalibrationNotificationManager> InNotificationManager,
											  TStrongObjectPtr<UFootageCaptureData> OutCaptureData)
{
	TArray<FCameraCalibration> MatchedCalibrations =
		MatchImageSequenceWithCalibration(OutCaptureData->ImageSequences, InCameraCalibrations);

	TObjectPtr<UCameraCalibration> CalibrationAsset =
		UE::MetaHuman::Private::CreateCameraCalibrationAsset(InOptions->PackagePath.Path, InOptions->AssetName);

	CalibrationAsset->CameraCalibrations.Reset();
	CalibrationAsset->StereoPairs.Reset();
	CalibrationAsset->ConvertFromTrackerNodeCameraModels(MatchedCalibrations, false);

	OutCaptureData->CameraCalibrations.Add(MoveTemp(CalibrationAsset));

	OutCaptureData->MarkPackageDirty();

	AddMetadataInformation(InOwner, InOptions, OutCaptureData, CalibrationAsset);

	if (InOptions->bAutoSaveAssets)
	{
		SaveCalibrationProcessCreatedAssets(InOptions->PackagePath.Path);
	}
}

static void CreateCalibrationAssetOnGameThread(TStrongObjectPtr<const UMetaHumanCalibrationGenerator> InOwner,
											   TStrongObjectPtr<const UMetaHumanCalibrationGeneratorOptions> InOptions,
											   TArray<FCameraCalibration> InCameraCalibrations,
											   TSharedPtr<FMetaHumanCalibrationNotificationManager> InNotificationManager,
											   TStrongObjectPtr<UFootageCaptureData> OutCaptureData)
{
	if (IsInGameThread())
	{
		CreateCalibrationAsset_GameThread(
			MoveTemp(InOwner),
			MoveTemp(InOptions),
			MoveTemp(InCameraCalibrations),
			MoveTemp(InNotificationManager),
			MoveTemp(OutCaptureData));
	}
	else
	{
		// UMetaHumanCalibrationGenerated, UFootageCaptureData and FCalibrationNotificationManager are captured here to protect their lifecycle. 
		ExecuteOnGameThread(TEXT("CalibrationAssetCreation"),
							[Owner = MoveTemp(InOwner),
							Options = MoveTemp(InOptions),
							CaptureData = MoveTemp(OutCaptureData),
							CameraCalibrations = MoveTemp(InCameraCalibrations),
							NotificationManager = MoveTemp(InNotificationManager)]() mutable
							{
								CreateCalibrationAsset_GameThread(
									MoveTemp(Owner),
									MoveTemp(Options),
									MoveTemp(CameraCalibrations),
									MoveTemp(NotificationManager),
									MoveTemp(CaptureData));
							});
	}
}

}

UMetaHumanCalibrationGenerator::UMetaHumanCalibrationGenerator()
	: StereoCalibrator(MakeShared<UE::Wrappers::FMetaHumanStereoCalibrator>())
{
}

bool UMetaHumanCalibrationGenerator::Init(const UMetaHumanCalibrationGeneratorConfig* InConfig)
{
	TValueOrError<void, FString> ConfigValidity = InConfig->CheckConfigValidity();

	if (ConfigValidity.HasError())
	{
		ErrorMessage = FString::Printf(TEXT("Invalid config for stereo calibration process: %s"), *ConfigValidity.GetError());

		UE_LOGF(LogMetaHumanCalibrationGenerator, Error, "%ls", *ErrorMessage);
		return false;
	}

	bInitialized = StereoCalibrator->Init(InConfig->BoardPatternWidth - 1, InConfig->BoardPatternHeight - 1, InConfig->BoardSquareSize);
	return bInitialized;
}

bool UMetaHumanCalibrationGenerator::ConfigureCameras(const UFootageCaptureData* InCaptureData)
{
	bCamerasConfigured = false;

	if (InCaptureData->ImageSequences.Num() < 2)
	{
		ErrorMessage = FString::Printf(TEXT("Stereo calibration process expects 2 cameras, but found %d"), InCaptureData->ImageSequences.Num());

		UE_LOGF(LogMetaHumanCalibrationGenerator, Error, "%ls", *ErrorMessage);
		return false;
	}

	const UImgMediaSource* FirstCameraImageSource = InCaptureData->ImageSequences[0];
	const UImgMediaSource* SecondCameraImageSource = InCaptureData->ImageSequences[1];

	if (!IsValid(FirstCameraImageSource) || !IsValid(SecondCameraImageSource))
	{
		ErrorMessage = TEXT("No valid cameras found");

		UE_LOGF(LogMetaHumanCalibrationGenerator, Error, "%ls", *ErrorMessage);
		return false;
	}

	FString FirstCameraName = UE::MetaHuman::Image::GetCameraId(FirstCameraImageSource);
	FString SecondCameraName = UE::MetaHuman::Image::GetCameraId(SecondCameraImageSource);

	FIntVector2 FirstCameraImageDimensions;
	int32 NumberOfImages = 0;
	FImageSequenceUtils::GetImageSequenceInfoFromAsset(FirstCameraImageSource, FirstCameraImageDimensions, NumberOfImages);

	FIntVector2 SecondCameraImageDimensions;
	FImageSequenceUtils::GetImageSequenceInfoFromAsset(SecondCameraImageSource, SecondCameraImageDimensions, NumberOfImages);

	UE_LOGF(LogMetaHumanCalibrationGenerator, Display, "Adding %ls camera with image size %dx%d", *FirstCameraName, FirstCameraImageDimensions.X, FirstCameraImageDimensions.Y);
	StereoCalibrator->AddCamera(FirstCameraName, FirstCameraImageDimensions.X, FirstCameraImageDimensions.Y);

	UE_LOGF(LogMetaHumanCalibrationGenerator, Display, "Adding %ls camera with image size %dx%d", *SecondCameraName, SecondCameraImageDimensions.X, SecondCameraImageDimensions.Y);
	StereoCalibrator->AddCamera(SecondCameraName, SecondCameraImageDimensions.X, SecondCameraImageDimensions.Y);

	bCamerasConfigured = true;
	return bCamerasConfigured;
}

bool UMetaHumanCalibrationGenerator::Process(UFootageCaptureData* InCaptureData, const UMetaHumanCalibrationGeneratorOptions* InOptions)
{
	using namespace UE::MetaHuman::Private;
	using namespace UE::MetaHuman::Image;
	using namespace UE::CaptureManager;

	if (!bInitialized)
	{
		// Backwards compatibility with old Options.
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		UMetaHumanCalibrationGeneratorConfig* Config = NewObject<UMetaHumanCalibrationGeneratorConfig>();
		Config->BoardPatternHeight = InOptions->BoardPatternHeight_DEPRECATED + 1;
		Config->BoardPatternWidth = InOptions->BoardPatternWidth_DEPRECATED + 1;
		Config->BoardSquareSize = InOptions->BoardSquareSize_DEPRECATED;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

		Init(Config);
	}

	TValueOrError<void, FString> OptionsValidity = InOptions->CheckOptionsValidity();
	if (OptionsValidity.HasError())
	{
		ErrorMessage = FString::Printf(TEXT("Invalid options for stereo calibration process: %s"), *OptionsValidity.GetError());

		UE_LOGF(LogMetaHumanCalibrationGenerator, Error, "%ls", *ErrorMessage);
		return false;
	}

	if (!bCamerasConfigured)
	{
		bool bConfigured = ConfigureCameras(InCaptureData);
		if (!bConfigured)
		{
			return false;
		}
	}

	TOptional<FMetaHumanCalibrationFrameResolver> ResolverOpt = FMetaHumanCalibrationFrameResolver::CreateFromCaptureData(InCaptureData);
	if (!ResolverOpt.IsSet())
	{
		ErrorMessage = TEXT("Frame Resolver is NOT valid.");

		UE_LOGF(LogMetaHumanCalibrationGenerator, Error, "%ls", *ErrorMessage);
		return false;
	}

	FMetaHumanCalibrationFrameResolver Resolver = MoveTemp(ResolverOpt.GetValue());

	if (!Resolver.HasFrames())
	{
		ErrorMessage = TEXT("No matching frames found.");

		UE_LOGF(LogMetaHumanCalibrationGenerator, Error, "%ls", *ErrorMessage);
		return false;
	}

	FString FirstCameraName = GetCameraId(InCaptureData->ImageSequences[0]);
	FString SecondCameraName = GetCameraId(InCaptureData->ImageSequences[1]);

	using FFrameCameraPaths = TPair<TArray<FString>, TArray<FString>>;

	FFrameCameraPaths AllFramePaths;
	Resolver.GetFramePathsForCameraIndex(0, AllFramePaths.Key);
	Resolver.GetFramePathsForCameraIndex(1, AllFramePaths.Value);

	TSharedPtr<FMetaHumanCalibrationNotificationManager> NotificationManager = 
		MakeShared<FMetaHumanCalibrationNotificationManager>();
	NotificationManager->NotificationOnBegin(LOCTEXT("CalibrationDetectionInProgress", "MetaHumanCalibrationGenerator: Waiting for chessboard pattern detection..."));

	FFrameCameraPaths SelectedFramePaths;
	if (!InOptions->SelectedFrames.IsEmpty())
	{
		SelectedFramePaths = FilterFramePaths(AllFramePaths, [&](int32 InFrameIndex) -> bool
		{
			return InOptions->SelectedFrames.Contains(InFrameIndex);
		});
	}
	else
	{
		SelectedFramePaths = FilterFramePaths(AllFramePaths, [&](int32 InFrameIndex) -> bool
		{
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			bool bShouldIncludeFrame = (InFrameIndex % InOptions->SampleRate_DEPRECATED) == 0;
			PRAGMA_ENABLE_DEPRECATION_WARNINGS

			return bShouldIncludeFrame;
		});
	}

	TUniquePtr<FMetaHumanCalibrationPatternDetector> PatternDetector =
		FMetaHumanCalibrationPatternDetector::CreateFromExistingCalibrator(StereoCalibrator);

	auto OnFailureLambda = [AllFramePaths = MoveTemp(AllFramePaths), &SelectedFramePaths](const FMetaHumanCalibrationPatternDetector::FFramePaths& InFailedPaths, int32 InTry)
		-> TOptional<FMetaHumanCalibrationPatternDetector::FFramePaths>
		{
			int32 FirstCameraPathIndex = AllFramePaths.Key.IndexOfByKey(InFailedPaths.Key);
			int32 SecondCameraPathIndex = AllFramePaths.Value.IndexOfByKey(InFailedPaths.Value);

			check(FirstCameraPathIndex == SecondCameraPathIndex);

			const int32 FrameIndex = FirstCameraPathIndex;

			if (FrameIndex == INDEX_NONE)
			{
				return {};
			}

			const int32 NewFrameIndex = FrameIndex + InTry;

			FString NewFirstCameraPath;
			FString NewSecondCameraPath;

			if (AllFramePaths.Key.IsValidIndex(NewFrameIndex) && 
				AllFramePaths.Value.IsValidIndex(NewFrameIndex))
			{
				NewFirstCameraPath = AllFramePaths.Key[NewFrameIndex];
				NewSecondCameraPath = AllFramePaths.Value[NewFrameIndex];
			}

			if (NewFirstCameraPath.IsEmpty() || NewSecondCameraPath.IsEmpty())
			{
				return {};
			}

			if (SelectedFramePaths.Key.Contains(NewFirstCameraPath) ||
				SelectedFramePaths.Value.Contains(NewSecondCameraPath))
			{
				return {};
			}

			FMetaHumanCalibrationPatternDetector::FFramePaths NewFrame = { MoveTemp(NewFirstCameraPath), MoveTemp(NewSecondCameraPath) };
			return NewFrame;
		};

	FMetaHumanCalibrationPatternDetector::FOnFailureFrameProvider OnFailureFrameProvider =
		FMetaHumanCalibrationPatternDetector::FOnFailureFrameProvider::CreateLambda(MoveTemp(OnFailureLambda));

	FMetaHumanCalibrationPatternDetector::FDetectedFrames DetectedFrames =
		PatternDetector->DetectPatterns({ FirstCameraName, SecondCameraName }, SelectedFramePaths, InOptions->SharpnessThreshold, MoveTemp(OnFailureFrameProvider));

	PatternDetector = nullptr;

	static constexpr int32 MinimumRequiredFrames = 3;
	bool bDetectionSuccess = !DetectedFrames.IsEmpty() && (DetectedFrames.Num() >= MinimumRequiredFrames);
	
	NotificationManager->NotificationOnEnd(bDetectionSuccess);

	if (!bDetectionSuccess)
	{
		ErrorMessage = FString::Printf(TEXT("Not enough valid frames detected to run calibration (Minimum is %d)"), MinimumRequiredFrames);

		UE_LOGF(LogMetaHumanCalibrationGenerator, Error, "%ls", *ErrorMessage);
		return false;
	}

	NotificationManager->NotificationOnBegin(LOCTEXT("CalibrationInProgress", "MetaHumanCalibrationGenerator: Waiting for calibration..."));
	TArray<FCameraCalibration> CameraCalibrations;
	double OutReprojectionError = 0.0f;

	TArray<FMetaHumanCalibrationPatternDetector::FDetectedFrame> DetectedFramesArray;
	DetectedFrames.GenerateValueArray(DetectedFramesArray);

	bool bResult = StereoCalibrator->Calibrate(DetectedFramesArray, CameraCalibrations, OutReprojectionError);

	TOptional<FText> NewMessage = bResult ? FText::Format(LOCTEXT("CalibrationSuccess", "Calibrated Successfully {0}"), OutReprojectionError) : TOptional<FText>();
	NotificationManager->NotificationOnEnd(bResult, MoveTemp(NewMessage));

	if (!bResult)
	{
		ErrorMessage = TEXT("Failed to calibrate the footage");

		UE_LOGF(LogMetaHumanCalibrationGenerator, Error, "%ls", *ErrorMessage);
		return false;
	}

	UE_LOGF(LogMetaHumanCalibrationGenerator, Display, "Successfully calibrated with reprojection error of %lf", OutReprojectionError);

	LastRMSError = OutReprojectionError;

	CreateCalibrationAssetOnGameThread(TStrongObjectPtr<const UMetaHumanCalibrationGenerator>(this),
									   TStrongObjectPtr<const UMetaHumanCalibrationGeneratorOptions>(InOptions),
									   MoveTemp(CameraCalibrations),
									   MoveTemp(NotificationManager), 
									   TStrongObjectPtr<UFootageCaptureData>(InCaptureData));

	return true;
}

double UMetaHumanCalibrationGenerator::GetLastRMSError() const
{
	return LastRMSError;
}

FString UMetaHumanCalibrationGenerator::GetLastError()
{
	FString CurrentErrorMessage = ErrorMessage;
	ErrorMessage.Reset();

	return CurrentErrorMessage;
}

bool UMetaHumanCalibrationGenerator::Reset(const UMetaHumanCalibrationGeneratorConfig* InConfig, const UFootageCaptureData* InCaptureData)
{
	StereoCalibrator = MakeShared<UE::Wrappers::FMetaHumanStereoCalibrator>();

	bool bSuccess = Init(InConfig);

	if (!bSuccess)
	{
		return false;
	}

	bSuccess = ConfigureCameras(InCaptureData);

	return bSuccess;
}

bool UMetaHumanCalibrationGenerator::ExportCalibration(UCameraCalibration* InCameraCalibration, const FDirectoryPath& InPath)
{
	if (!InCameraCalibration)
	{
		UE_LOGF(LogMetaHumanCalibrationGenerator, Error, "Invalid camera calibration provided");
		return false;
	}

	if (InPath.Path.IsEmpty())
	{
		UE_LOGF(LogMetaHumanCalibrationGenerator, Error, "Invalid path provided");
		return false;
	}

	TSharedRef<UE::Wrappers::FMetaHumanStereoCalibrator> StereoCalibrator =
		MakeShared<UE::Wrappers::FMetaHumanStereoCalibrator>();

	TArray<FCameraCalibration> Calibrations;
	TArray<TPair<FString, FString>> StereoReconstructionPairs;
	InCameraCalibration->ConvertToTrackerNodeCameraModels(Calibrations, StereoReconstructionPairs);

	static constexpr FStringView CalibFileName = TEXT("calib.json");
	const FString BaseDirectory = InPath.Path / InCameraCalibration->GetName();

	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.DirectoryExists(*BaseDirectory))
	{
		bool bSuccess = FileManager.MakeDirectory(*BaseDirectory, true);
		if (!bSuccess)
		{
			UE_LOGF(LogMetaHumanCalibrationGenerator, Error, "Failed to create a directory: %ls", *BaseDirectory);
			return false;
		}
	}

	const FString FullFilePath = BaseDirectory / FString(CalibFileName);
	return StereoCalibrator->ExportCalibrations(Calibrations, FullFilePath);
}

#undef LOCTEXT_NAMESPACE
