// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorLandmarkTracker.h"

#include "IImageWrapperModule.h"
#include "LandmarkConfigIdentityHelper.h"
#include "MetaHumanCharacterEditorLog.h"
#include "MetaHumanContourDataVersion.h"
#include "MetaHumanIdentityPose.h"
#include "Nodes/ImageUtilNodes.h"
#include "Nodes/HyprsenseNode.h"

void UMetaHumanCharacterEditorLandmarkTracker::PostInitProperties()
{
	Super::PostInitProperties();

	TrackPipeline = MakeUnique<UE::MetaHuman::Pipeline::FPipeline>();

	if (!FaceContourTracker)
	{
		FaceContourTracker = UMetaHumanFaceContourTrackerAsset::LoadDefaultTracker();
	}
}

void UMetaHumanCharacterEditorLandmarkTracker::LoadTrackers()
{
	if (FaceContourTracker)
	{
		constexpr bool bShowProgress = true;
		FaceContourTracker->LoadTrackers(bShowProgress, [=](bool bTrackersLoaded)
		{
			if (!bTrackersLoaded)
			{
				UE_LOGF(LogMetaHumanCharacterEditor, Warning, "Failed to load trackers");
			}
		});
	}
}

FFrameTrackingContourData UMetaHumanCharacterEditorLandmarkTracker::TrackImage(const TArray<FColor>& InImageData, int32 InWidth, int32 InHeight)
{
	InitializeContourData(InWidth, InHeight);

	FFrameTrackingContourData TrackingDataResult;

	if (!FaceContourTracker || !FaceContourTracker->CanProcess())
	{
		UE_LOGF(LogMetaHumanCharacterEditor, Warning, "Face contour trackers not loaded");
		return TrackingDataResult;
	}
	
	TrackPipeline->Reset();
	const TSharedPtr<UE::MetaHuman::Pipeline::FFColorToUEImageNode> UEImage = TrackPipeline->MakeNode<UE::MetaHuman::Pipeline::FFColorToUEImageNode>("RenderTarget");
	const TSharedPtr<UE::MetaHuman::Pipeline::FHyprsenseNode> GenericTracker = TrackPipeline->MakeNode<UE::MetaHuman::Pipeline::FHyprsenseNode>("GenericTracker");

	UEImage->Samples = InImageData;
	UEImage->Width = InWidth;
	UEImage->Height = InHeight;
	
	// Handle back-compatibility of GPU models stored in UMetaHumanFaceContourTrackerAsset.
	// Define an effective IModelInstanceRunSync to use that is either the deprecated IModelInstanceGPU
	// if set or the new IModelInstanceRunSync if not.

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	#define EFFECTIVE_TRACKER(TRACKER) TSharedPtr<UE::NNE::IModelInstanceRunSync> TRACKER = FaceContourTracker->TRACKER.IsValid() ? FaceContourTracker->TRACKER : FaceContourTracker->TRACKER##Model;
		EFFECTIVE_TRACKER(FullFaceTracker)
		EFFECTIVE_TRACKER(FaceDetector)
		EFFECTIVE_TRACKER(BrowsDenseTracker)
		EFFECTIVE_TRACKER(EyesDenseTracker)
		EFFECTIVE_TRACKER(MouthDenseTracker)
		EFFECTIVE_TRACKER(LipzipDenseTracker)
		EFFECTIVE_TRACKER(NasioLabialsDenseTracker)
		EFFECTIVE_TRACKER(ChinDenseTracker)
		EFFECTIVE_TRACKER(TeethDenseTracker)
		EFFECTIVE_TRACKER(TeethConfidenceTracker)
	#undef EFFECTIVE_TRACKER
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	bool bSetTrackersSuccessfully = GenericTracker->SetTrackers(FullFaceTracker,
		FaceDetector,
		BrowsDenseTracker,
		EyesDenseTracker,
		MouthDenseTracker,
		LipzipDenseTracker,
		NasioLabialsDenseTracker,
		ChinDenseTracker,
		TeethDenseTracker,
		TeethConfidenceTracker);

	if (!bSetTrackersSuccessfully)
	{
		// a standard pipeline 'Failed to start' error will be triggered but we display this information in the log 
		// so that the user can act (for example if a custom tracker asset has not been set up correctly)
		UE_LOGF(LogMetaHumanCharacterEditor, Error, "%ls", *GenericTracker->GetErrorMessage());
	}
	
	TrackPipeline->MakeConnection(UEImage, GenericTracker);
	
	UE::MetaHuman::Pipeline::FFrameComplete OnFrameComplete;
	UE::MetaHuman::Pipeline::FProcessComplete OnProcessComplete;
	
	const FString TrackingResultsPinName = GenericTracker->Name + ".Contours Out";

	OnFrameComplete.AddLambda([TrackingResultsPinName, &TrackingDataResult](const TSharedPtr<UE::MetaHuman::Pipeline::FPipelineData> InPipelineData)
	{
		TrackingDataResult = InPipelineData->GetData<FFrameTrackingContourData>(TrackingResultsPinName);
	});

	OnProcessComplete.AddLambda([this](TSharedPtr<UE::MetaHuman::Pipeline::FPipelineData> InPipelineData)
	{
		if (InPipelineData->GetExitStatus() != UE::MetaHuman::Pipeline::EPipelineExitStatus::Ok)
		{
			UE_LOGF(LogMetaHumanCharacterEditor, Warning, "Tracking process failed");
		}
		
		TrackPipeline->Reset();
	});

	UE::MetaHuman::Pipeline::FPipelineRunParameters PipelineRunParameters;
	PipelineRunParameters.SetStartFrame(0);
	PipelineRunParameters.SetEndFrame(1);
	PipelineRunParameters.SetOnFrameComplete(OnFrameComplete);
	PipelineRunParameters.SetOnProcessComplete(OnProcessComplete);
	PipelineRunParameters.SetGpuToUse(UE::MetaHuman::Pipeline::FPipeline::PickPhysicalDevice());
	PipelineRunParameters.SetMode(UE::MetaHuman::Pipeline::EPipelineMode::PushSyncNodes);

	TrackPipeline->Run(PipelineRunParameters);

	return TrackingDataResult;
}

TMap<FString, TArray<FVector2D>> UMetaHumanCharacterEditorLandmarkTracker::GetTrackingContours(const FFrameTrackingContourData& TrackingContourData) const
{
	TMap<FString, TArray<FVector2D>> TrackingPoints;
	if (CurveDataController)
	{
		CurveDataController->UpdateFromContourData(TrackingContourData, true);
		TrackingPoints = CurveDataController->GetDensePointsForVisibleCurves();
	}

	return TrackingPoints;
}

void UMetaHumanCharacterEditorLandmarkTracker::InitializeContourData(int32 InWidth, int32 InHeight)
{
	/*RF_Transactional so that ContourData->Modify() calls(used by the shared STrackerImageViewer / FMetaHumanPointDragOperation to scope drag - based undo)
	actually record the object state into the transaction buffer. Without this flag, those Modify() calls are silently dropped.*/
	ContourData = NewObject<UMetaHumanContourData>(this, NAME_None, RF_Transactional);
	CurveDataController = MakeShared<FMetaHumanCurveDataController>(ContourData);
	const FVector2D ImageSize = FVector2D(InWidth, InHeight);
	TrackingImageSize = ImageSize;
	
	FLandmarkConfigIdentityHelper ConfigIdentityHelper;
	FFrameTrackingContourData DefaultContourData = ConfigIdentityHelper.GetDefaultContourDataFromConfig(TrackingImageSize, ECurvePresetType::NeutralPose_NoBrowsNoIris);
	const FString ConfigVersion = FMetaHumanContourDataVersion::GetContourDataVersionString();
	CurveDataController->InitializeContoursFromConfig(DefaultContourData, ConfigVersion);
}
