// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Pipeline/Pipeline.h"
#include "Pipeline/PipelineData.h"
#include "Nodes/TongueTrackerNode.h"
#include "Nodes/ControlUtilNodes.h"
#include "Nodes/HyprsenseRealtimeNode.h"
#include "Nodes/RealtimeSpeechToAnimNode.h"
#include "CaptureData.h"
#include "FrameRange.h"
#include "NNEModelData.h"
#ifdef WITH_EDITOR
#include "FrameRangeArrayBuilder.h"
#endif
#include "MetaHumanRealtimeSmoothing.h"
#include "MetaHumanRealtimeCalibration.h"
#include "MetaHumanBodyTrackerInterface.h"

#include "SequencedImageTrackInfo.h"

#include "Rigs/RigHierarchyElements.h"
#include "ControlRigAssetReference.h"
#include "MetaHumanPerformance.generated.h"

/////////////////////////////////////////////////////
// UMetaHumanPerformance


enum class EPerformanceExportRange : uint8;

UENUM(BlueprintType)
enum class EDataInputType : uint8
{
	DepthFootage	UMETA(DisplayName = "Depth Footage", ToolTip = "Process depth enabled footage and an identity into animation"),
	Audio			UMETA(DisplayName = "Audio", ToolTip = "Process audio into animation"),
	MonoFootage		UMETA(DisplayName = "Monocular Footage", ToolTip = "Process single view footage into animation")
};

UENUM()
enum class ESolveType : uint8
{
	Preview				UMETA(DisplayName = "Preview"),
	Standard			UMETA(DisplayName = "Standard"),
	AdditionalTweakers	UMETA(DisplayName = "Additional Tweakers"),
	AdditionalTweakersPlusChinCompress	UMETA(DisplayName = "Additional Tweakers plus Chin Compress"),
};

UENUM()
enum class EPerformanceHeadMovementMode : uint8
{
	/** Use a transform track to move the Skeletal Mesh based on its pivot point (root bone) */
	TransformTrack,

	/** Enables the Head Control Switch in the Control Rig to use control rig for the Head Movement */
	ControlRig,

	/** No head movement */
	Disabled
};

UENUM()
enum class EStartPipelineErrorType : uint8
{
	None,
	NoFrames,
	Disabled,
};

/** MetaHuman Performance Asset
* 
*   Produces an Animation Sequence for MetaHuman Control Rig by tracking
*   facial expressions in video-footage from a Capture Source, imported
*   through Capture Manager, using a SkeletalMesh obtained through
*   MetaHuman Identity asset toolkit.
* 
*/
UCLASS(MinimalAPI, BlueprintType)
class UMetaHumanPerformance : public UObject
{
	GENERATED_BODY()

public:

	METAHUMANPERFORMANCE_API UMetaHumanPerformance();

	//~Begin UObject interface
	METAHUMANPERFORMANCE_API virtual void BeginDestroy() override;
	METAHUMANPERFORMANCE_API virtual void PreEditChange(FEditPropertyChain& InPropertyAboutToChange) override;
	METAHUMANPERFORMANCE_API virtual void PostEditChangeProperty(struct FPropertyChangedEvent& InPropertyChangedEvent) override;
	METAHUMANPERFORMANCE_API virtual void PostEditUndo() override;
	METAHUMANPERFORMANCE_API virtual void PostInitProperties() override;
	METAHUMANPERFORMANCE_API virtual void PostLoad() override;
	METAHUMANPERFORMANCE_API virtual void Serialize(FArchive& Ar) override;
#if WITH_EDITOR
	METAHUMANPERFORMANCE_API virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif
	METAHUMANPERFORMANCE_API virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~End UObject interface

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnProcessingFinishedDynamic);

	// Dynamic delegate called when the pipeline finishes running
	UPROPERTY(Transient, BlueprintAssignable, Category = "Processing")
	FOnProcessingFinishedDynamic OnProcessingFinishedDynamic;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Export")
	METAHUMANPERFORMANCE_API bool CanExportAnimation() const;

	UE_DEPRECATED(5.1, "ExportAnimation has been deprecated, please use UMetaHumanPerformanceExportUtils::ExportAnimation instead")
	/**
	 * (DEPRECATED: use UMetaHumanPerformanceExportUtils::ExportAnimation instead)
	 * Export an animation sequence targeting the face skeleton. This will ask the user where to place the new animation sequence
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Export")
	METAHUMANPERFORMANCE_API void ExportAnimation(EPerformanceExportRange InExportRange);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Diagnostics")
	METAHUMANPERFORMANCE_API bool DiagnosticsIndicatesProcessingIssue(FText& OutDiagnosticsWarningMessage) const;

	// Event delegates
#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnDataInputTypeChanged, EDataInputType InProcessingType);
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnSourceDataChanged, class UFootageCaptureData* InFootageCaptureData, class USoundWave* InAudio, bool InResetRanges);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnIdentityChanged, class UMetaHumanIdentity* InIdentity);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnVisualizeMeshChanged, class USkeletalMesh* InVisualizeMesh);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnVisualizeObjectChanged, class UObject* InVisualizeObject);
	DECLARE_MULTICAST_DELEGATE(FOnSkeletonParamsChanged);
	DECLARE_MULTICAST_DELEGATE_FourParams(FOnDepthChanged, float InDepthDataNear, float InDepthDataFar, float InDepthMeshNear, float InDepthMeshFar);
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnFrameRangeChanged, int32 InStartFrame, int32 InEndFrame);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnRealtimeAudioChanged, bool bInRealtimeAudio);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnFrameProcessed, int32 InFrame);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnProcessingFinished, TSharedPtr<const UE::MetaHuman::Pipeline::FPipelineData> InPipelineData);
	DECLARE_MULTICAST_DELEGATE(FOnStage1ProcessingFinished);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnStageProcessingFinished, int32 InCurrentStage);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnControlRigClassChanged, TSubclassOf<class UControlRig> InControlRigClass);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnControlRigAssetReferenceChanged, const FControlRigAssetStrongReference& InControlRigAssetReference);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnHeadMovementModeChanged, EPerformanceHeadMovementMode InHeadMovementMode);
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnHeadMovementReferenceFrameChanged, bool bInAutoChooseHeadMovementReferenceFrame, uint32 InHeadMovementReferenceFrame);
	DECLARE_MULTICAST_DELEGATE(FOnNeutralPoseCalibrationChanged);
	DECLARE_MULTICAST_DELEGATE(FOnExcludedFramesChanged);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnBodyTrackerModeChanged, EMetaHumanBodyTrackerMode InBodyTrackerMode);
	DECLARE_DELEGATE_RetVal(USkeletalMesh*, FOnGetHeadMesh);

	FOnDataInputTypeChanged& OnDataInputTypeChanged() { return OnDataInputTypeChangedDelegate; }
	FOnSourceDataChanged& OnSourceDataChanged() { return OnSourceDataChangedDelegate; }
	FOnIdentityChanged& OnIdentityChanged() { return OnIdentityChangedDelegate; }
	UE_DEPRECATED(5.8, "This function is deprecated. Please use OnVisualizeObjectChanged instead.")
	FOnVisualizeMeshChanged& OnVisualizeMeshChanged() { return OnVisualizeMeshChangedDelegate; }
	FOnVisualizeObjectChanged& OnVisualizeObjectChanged() { return OnVisualizeObjectChangedDelegate; }
	FOnSkeletonParamsChanged& OnSkeletonParamsChanged() { return OnSkeletonParamsChangedDelegate; }
	FOnFrameRangeChanged& OnFrameRangeChanged() { return OnFrameRangeChangedDelegate; }
	FOnRealtimeAudioChanged& OnRealtimeAudioChanged() { return OnRealtimeAudioChangedDelegate; }
	FOnFrameProcessed& OnFrameProcessed() { return OnFrameProcessedDelegate; }
	FOnProcessingFinished& OnProcessingFinished() { return OnProcessingFinishedDelegate; }
	UE_DEPRECATED(5.8, "This function is deprecated. Please use OnStageProcessingFinished instead.")
	FOnStage1ProcessingFinished& OnStage1ProcessingFinished() { return OnStage1ProcessingFinishedDelegate; }
	FOnStageProcessingFinished& OnStageProcessingFinished() { return OnStageProcessingFinishedDelegate; }
	UE_DEPRECATED(5.8, "This function is deprecated. Please use OnControlRigAssetReferenceChanged instead.")
	FOnControlRigClassChanged& OnControlRigClassChanged() { return OnControlRigClassChangedDelegate; }
	FOnControlRigAssetReferenceChanged& OnControlRigAssetReferenceChanged() { return OnControlRigAssetReferenceChangedDelegate; }
	FOnHeadMovementModeChanged& OnHeadMovementModeChanged() { return OnHeadMovementModeChangedDelegate; }
	FOnHeadMovementReferenceFrameChanged& OnHeadMovementReferenceFrameChanged() { return OnHeadMovementReferenceFrameChangedDelegate; }
	FOnNeutralPoseCalibrationChanged& OnNeutralPoseCalibrationChanged() { return OnNeutralPoseCalibrationChangedDelegate; }
	FOnExcludedFramesChanged& OnExcludedFramesChanged() { return OnExcludedFramesChangedDelegate; }
	FOnBodyTrackerModeChanged& OnBodyTrackerModeChanged() { return OnBodyTrackerModeChangedDelegate; }
	FFrameRangeArrayBuilder::FOnGetCurrentFrame& OnGetCurrentFrame() { return OnGetCurrentFrameDelegate; }
	FOnGetHeadMesh& OnGetHeadMesh() { return OnGetHeadMeshDelegate; }

	METAHUMANPERFORMANCE_API FFrameRate GetFrameRate() const;

	METAHUMANPERFORMANCE_API FString GetHashedPerformanceAssetID();

#endif

	/** Enum to indicate which data input type is being used for the performance */
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetInputType", Category = "Data")
	EDataInputType InputType = EDataInputType::DepthFootage;

	/** Real-world footage data with the performance */
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetFootageCaptureData", Category = "Data",
		meta = (EditCondition = "InputType != EDataInputType::Audio", EditConditionHides))
	TObjectPtr<class UFootageCaptureData> FootageCaptureData;

	/** Audio of performance used with the Audio data input type */
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetAudio", Category = "Data", meta = (EditCondition = "InputType == EDataInputType::Audio", EditConditionHides))
	TObjectPtr<class USoundWave> Audio;

	/** Getter for audio to use for processing. Either audio in FootageCaptureData or overridden by Audio */
	METAHUMANPERFORMANCE_API TObjectPtr<class USoundWave> GetAudioForProcessing() const;

	/** Getter for timecode from audio media */
	METAHUMANPERFORMANCE_API FTimecode GetAudioMediaTimecode() const;

	/** Getter for timecode rate from audio media */
	METAHUMANPERFORMANCE_API FFrameRate GetAudioMediaTimecodeRate() const;

	/** Display name of the config to use with the capture data */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Data", meta = (EditCondition = "InputType == EDataInputType::DepthFootage", EditConditionHides))
	FString CaptureDataConfig;

	/** Name of camera (view) in the footage capture data calibration to use for display and processing */
	UPROPERTY(EditAnywhere, Category = "Data", meta = (EditCondition = "InputType != EDataInputType::Audio", EditConditionHides))
	FString Camera;

	/** Timecode alignment type */
	UPROPERTY(EditAnywhere, Category = "Data")
	ETimecodeAlignment TimecodeAlignment = ETimecodeAlignment::Relative;

	/** A digital double of the person performing in the footage, captured in the MetaHuman Identity asset */
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetIdentity", Category = "Data", DisplayName = "MetaHuman Identity", meta = (EditCondition = "InputType == EDataInputType::DepthFootage", EditConditionHides))
	TObjectPtr<class UMetaHumanIdentity> Identity;

	/** Control Rig used to drive the animation */
	UPROPERTY()
	TObjectPtr<class UControlRigBlueprint> ControlRig_DEPRECATED;

#if WITH_EDITORONLY_DATA
	UPROPERTY(meta = (DeprecatedProperty))
	TSubclassOf<class UControlRig> ControlRigClass_DEPRECATED;
#endif

	UPROPERTY(EditAnywhere, Category = "Visualization", DisplayName = "Control Rig", BlueprintSetter = "SetControlRigAssetReference", meta = (GetAssetFilter="ShouldFilterControlRigAsset"))
	FControlRigAssetStrongReference ControlRigAssetReference;

	/** Set a different object (e.g. head skel mesh or full MetaHuman) for visualizing the final animation */
	UE_DEPRECATED(5.8, "This variable is deprecated. Please use VisualizationObject instead.")
	UPROPERTY()
	TObjectPtr<class USkeletalMesh> VisualizationMesh;

	UPROPERTY(EditAnywhere, Category = "Visualization", meta = (GetAssetFilter = "ShouldFilterVisualizationObject"))
	TObjectPtr<class UObject> VisualizationObject;

	UPROPERTY(EditAnywhere, Category = "Visualization")
	bool bShowSkeleton = true;

	UPROPERTY(EditAnywhere, Category = "Visualization", meta = (EditCondition = "bShowSkeleton"))
	FVector SkeletonOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Visualization", meta = (EditCondition = "bShowSkeleton"))
	FColor SkeletonColor = FColor::Purple;

	/** Head movement type */
	UPROPERTY(EditAnywhere, Category = "Visualization", meta = (EditCondition = "!bBodyTracking", EditConditionHides))
	EPerformanceHeadMovementMode HeadMovementMode = EPerformanceHeadMovementMode::TransformTrack;

	/** Which frame to use as reference frame for head pose (if Auto Choose Head Movement Reference Frame is not selected), default to first processed frame. Changing this will cause a re-bake of Control Rig data */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Visualization", meta = (EditCondition = "!bAutoChooseHeadMovementReferenceFrame"));
	uint32 HeadMovementReferenceFrame;

	/* If set to true, automatically pick the most front - facing frame as the reference frame for control-rig head movement calculation, default to true. Changing this will cause a re-bake of Control Rig data */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Visualization")
	uint8 bAutoChooseHeadMovementReferenceFrame : 1;

	/** Head reference frame, calculated from the two properties above. If set to -1, indicates it has not been calculated*/
	UPROPERTY(Transient)
	int32 HeadMovementReferenceFrameCalculated = -1;

	/* If set to true perform neutral pose calibration for mono solve, default to false. Changing this will cause a re-bake of Control Rig data */
	UPROPERTY(EditAnywhere, AdvancedDisplay, DisplayName = "Enable Neutral Pose Calibration", Category = "Visualization")
	bool bNeutralPoseCalibrationEnabled = false;

	/* Which frame to use as the neutral pose calibration for mono solve (if Enable Neutral Pose Calibration is selected), default to first processed frame. Changing this will cause a re-bake of Control Rig data */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Visualization")
	uint32 NeutralPoseCalibrationFrame = 0;

	/* Neutral pose calibration alpha parameter, defaults to 1. Changing this will cause a re-bake of Control Rig data */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Visualization", meta = (ClampMin = 0.0, ClampMax = 1.0))
	double NeutralPoseCalibrationAlpha = 1.0;

	/* Set of curve names to apply neutral pose calibration to. Changing this will cause a re-bake of Control Rig data */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Visualization")
	TArray<FName> NeutralPoseCalibrationCurves = FMetaHumanRealtimeCalibration::GetDefaultProperties();

	/* Tracker parameters for processing the footage */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters", meta = (EditCondition = "InputType == EDataInputType::DepthFootage", EditConditionHides))
	TObjectPtr<class UMetaHumanFaceContourTrackerAsset> DefaultTracker;

	/* Solver parameters for processing the footage */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters", meta = (EditCondition = "InputType == EDataInputType::DepthFootage", EditConditionHides))
	TObjectPtr<class UMetaHumanFaceAnimationSolver> DefaultSolver;

	/* The frame to start processing from */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters")
	uint32 StartFrameToProcess;

	/* The frame to end processing with */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters")
	uint32 EndFrameToProcess;

	/* Enum to indicate which type of solve to perform */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters", meta = (EditCondition = "InputType == EDataInputType::DepthFootage", EditConditionHides))
	ESolveType SolveType = ESolveType::AdditionalTweakers;

	/* Flag indicating whether we want to produce curves containing combinations of controls which are more easily editable than the normal solve result*/
	UPROPERTY(EditAnywhere, Category = "Processing Parameters", meta = (EditCondition = "InputType == EDataInputType::DepthFootage", EditConditionHides))
	bool bEasyToEditControlCurves = false;

	/* Flag indicating if performance predictive solver preview should be ran */
	UPROPERTY(EditAnywhere, DisplayName = "Run Preview Pass", Category = "Processing Parameters", 
			  meta = (EditCondition = "InputType == EDataInputType::DepthFootage || (InputType == EDataInputType::MonoFootage && bBodyTracking)", EditConditionHides))
	bool bSkipPreview = false;

	/* Flag indicating if process should run animation filtering */
	UPROPERTY(EditAnywhere, DisplayName = "Filter Animation", Category = "Processing Parameters", 
			  meta = (EditCondition = "InputType == EDataInputType::DepthFootage", EditConditionHides))
	bool bSkipFiltering = false;

	/* Flag indicating if facial solving should be performed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Processing Parameters",
			  meta = (EditCondition = "InputType == EDataInputType::MonoFootage", EditConditionHides))
	bool bFaceTracking = true;

	/* Flag indicating if the process should run tongue solving */
	UPROPERTY(EditAnywhere, DisplayName = "Solve Tongue", Category = "Processing Parameters", 
			  meta = (EditCondition = "InputType == EDataInputType::DepthFootage || InputType == EDataInputType::MonoFootage", EditConditionHides))
	bool bSkipTongueSolve = false;

	/* Flag indicating if body tracking should be performed */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters", BlueprintSetter = "SetBodyTracking",
			  meta = (EditCondition = "InputType == EDataInputType::MonoFootage", EditConditionHides))
	bool bBodyTracking = false;

	/* Flag indicating if body height should be automatically inferred */
	UPROPERTY(EditAnywhere, DisplayName = "Auto Body Height", Category = "Processing Parameters",
			  meta = (EditCondition = "InputType == EDataInputType::MonoFootage", EditConditionHides))
	bool bAutoBodyHeight = true;

	/* Manual Body height. Values outside the range [145, 190] might lead to reduced tracking quality */
	UPROPERTY(EditAnywhere, DisplayName = "Body Height", Category = "Processing Parameters", meta = (Units = "Centimeters", ClampMin=1, ClampMax=1000, NoSpinbox=true))
	float BodyHeight = 180.0f;

	/* Threshold for acquiring (or re-acquiring) the subject.
	 * Higher values demand a clearly visible subject before tracking begins; lower values allow tracking to start on difficult footage but risk locking onto the wrong subject. */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters",
			  meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0",
					  EditCondition = "InputType == EDataInputType::MonoFootage", EditConditionHides))
	float BodyDetectionConfidence = 0.7f;

	/* Threshold for maintaining the subject lock across frames.
	 * Higher values drop the subject during occlusion or fast motion, producing gaps; lower values follow through difficult moments but may drift onto the background or another subject. */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters",
			  meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0",
					  EditCondition = "InputType == EDataInputType::MonoFootage", EditConditionHides))
	float BodyTrackingConfidence = 0.5f;

	/* Flag indicating if foot-locking should be enabled for the body solver */
	UPROPERTY(EditAnywhere, DisplayName = "Enable Foot-Locking", Category = "Processing Parameters",
		meta = (EditCondition = "InputType == EDataInputType::MonoFootage", EditConditionHides))
	bool bEnableFootLocking = true;

	/* Flag indicating if the process should run per-vertex solve (which is slow to process but gives slightly better animation results) */
	UPROPERTY(EditAnywhere, DisplayName = "Run Per Vertex Solve", Category = "Processing Parameters", 
			  meta = (EditCondition = "InputType == EDataInputType::DepthFootage", EditConditionHides))
	bool bSkipPerVertexSolve = true;

	/* Flag indicating if we should use realtime audio solve */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters", meta = (EditCondition = "InputType == EDataInputType::Audio", EditConditionHides))
	bool bRealtimeAudio = false;

	/* Downmix multi channel audio before solving into animation */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters", meta = (EditCondition = "InputType == EDataInputType::Audio && !bRealtimeAudio", EditConditionHides))
	bool bDownmixChannels = true;

	/* Specify the audio channel used to solve into animation */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters", meta = (ClampMin = 0, ClampMax = 64))
	uint32 AudioChannelIndex = 0;

	/* Flag indicating if we should generate blinks */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters", meta = (EditCondition = "InputType == EDataInputType::Audio && !bRealtimeAudio", EditConditionHides))
	bool bGenerateBlinks = true;

	UPROPERTY(EditAnywhere, Category = "Processing Parameters", meta = (DisplayName = "Process Mask", EditCondition = "InputType == EDataInputType::Audio && !bRealtimeAudio", EditConditionHides))
	EAudioDrivenAnimationOutputControls AudioDrivenAnimationOutputControls = EAudioDrivenAnimationOutputControls::FullFace;

	/* The models to be used by audio driven animation */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Processing Parameters", meta = (DisplayName = "Models", EditCondition = "InputType == EDataInputType::Audio && !bRealtimeAudio", EditConditionHides))
	FAudioDrivenAnimationModels AudioDrivenAnimationModels;

	/* The models to be used by the monocular pipeline */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Processing Parameters", meta = (DisplayName = "Facial Models", EditCondition = "InputType == EDataInputType::MonoFootage", EditConditionHides))
	FMonocularAnimationPipelineModels MonocularAnimationPipelineModels;
	
	/* The estimated focal length of the footage */
	UPROPERTY(VisibleAnywhere, Category = "Processing Parameters", meta = (EditCondition = "InputType == EDataInputType::MonoFootage", EditConditionHides))
	float FocalLength = -1;

	/* Reduces noise in head position and orientation. */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters", meta = (EditCondition = "InputType == EDataInputType::MonoFootage", EditConditionHides))
	bool bHeadStabilization = true;

	/* Allowed yaw (left/right) head rotation in degrees.
	 * Frames where the head turns past this angle are treated as unsolved.
	 * Changing this requires reprocessing to take effect. */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters",
			  meta = (Units = "Degrees", NoSpinbox = "true", UIMin = "0.0", UIMax = "90.0", ClampMin = "0.0", ClampMax = "90.0", EditCondition = "InputType == EDataInputType::MonoFootage", EditConditionHides))
	float HeadAllowedRotationLeftRight = 45.0;

	/* Allowed pitch (up/down) head rotation in degrees.
	 * Frames where the head tilts past this angle are treated as unsolved.
	 * Changing this requires reprocessing to take effect. */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters", 
			  meta = (Units = "Degrees", NoSpinbox = "true", UIMin = "0.0", UIMax = "90.0", ClampMin = "0.0", ClampMax = "90.0", EditCondition = "InputType == EDataInputType::MonoFootage", EditConditionHides))
	float HeadAllowedRotationUpDown = 30;

	/* How to handle frames where head rotation exceeds the allowed yaw or pitch.
	 *  - None: leave the frame empty; the exported curves will interpolate across the gap based on the curve interpolation setting.
	 *  - Neutral Pose: fall back to a neutral face pose.
	 * Changing this requires reprocessing to take effect. */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters", meta = (EditCondition = "InputType == EDataInputType::MonoFootage", EditConditionHides))
	EFaceUnsolvedFrameBehavior HeadRotationHandler = EFaceUnsolvedFrameBehavior::None;

	/* Smoothing parameters to use for mono video processing */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters", DisplayName = "Smoothing", meta = (EditCondition = "InputType == EDataInputType::MonoFootage", EditConditionHides))
	TObjectPtr<UMetaHumanRealtimeSmoothingParams> MonoSmoothingParams;

	/* Flag indicating if editor updates current frame to show the results as frames are processed */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters")
	bool bShowFramesAsTheyAreProcessed = true;

	/* Settings to change the behavior of the audio driven animation solve */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters", meta = (DisplayName = "Solve Overrides", ExpandByDefault, EditCondition = "InputType == EDataInputType::Audio && !bRealtimeAudio", EditConditionHides))
	FAudioDrivenAnimationSolveOverrides AudioDrivenAnimationSolveOverrides;

	/* The minimum cm from the camera expected for valid depth information. Depth information closer than this will be ignored to help filter out noise. */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters|Depth Generation", meta = (Units = "Centimeters", NoSpinbox = "true", UIMin = "0.0", UIMax = "200.0", EditCondition = "(InputType == EDataInputType::DepthFootage) && ShouldShowDepthParameters()", EditConditionHides))
	float MinDistance = 10.0;

	/* The maximum cm from the camera expected for valid depth information. Depth information beyond this will be ignored to help filter out noise. */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters|Depth Generation", meta = (Units = "Centimeters", NoSpinbox = "true", UIMin = "0.0", UIMax = "200.0", EditCondition = "(InputType == EDataInputType::DepthFootage) && ShouldShowDepthParameters()", EditConditionHides))
	float MaxDistance = 25.0;

	/* The mood of the realtime audio driven animation solve */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters|Solve Overrides", meta = (DisplayName = "Mood", EditCondition = "InputType == EDataInputType::Audio && bRealtimeAudio", EditConditionHides))
	EAudioDrivenAnimationMood RealtimeAudioMood = EAudioDrivenAnimationMood::AutoDetect;

	/* The mood intensity of the realtime audio driven animation solve */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters|Solve Overrides", meta = (UIMin = 0.0, ClampMin = 0.0, UIMax = 1.0, ClampMax = 1.0, Delta = 0.01, DisplayName = "Mood Intensity", EditCondition = "InputType == EDataInputType::Audio && bRealtimeAudio", EditConditionHides))
	float RealtimeAudioMoodIntensity = 1.0;

	/* The amount of time, in milliseconds, that the audio solver looks ahead into the audio stream to produce the current frame of animation. A larger value will produce higher quality animation but will come at the cost of increased latency. */
	UPROPERTY(EditAnywhere, Category = "Processing Parameters|Solve Overrides", meta = (UIMin = 80.0, ClampMin = 80.0, UIMax = 240.0, ClampMax = 240.0, Delta = 20.0, DisplayName = "Lookahead", EditCondition = "InputType == EDataInputType::Audio && bRealtimeAudio", EditConditionHides))
	int32 RealtimeAudioLookahead = 80.0;

	/* Flag indicating whether processing diagnostics should be calculated during processing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Compute Diagnostics", Category = "Processing Diagnostics", 
			  meta = (EditCondition = "InputType == EDataInputType::DepthFootage", EditConditionHides))
	bool bSkipDiagnostics = false;

	/* The minimum percentage of the face region which should have valid depth-map pixels. Below this value a diagnostic warning will be flagged. */
	UPROPERTY(EditAnywhere, Category = "Processing Diagnostics", meta = (UIMin = 0.0, UIMax = 100.0, ClampMin = 0.0, ClampMax = 100.0, EditCondition = "InputType == EDataInputType::DepthFootage", EditConditionHides))
	float MinimumDepthMapFaceCoverage = 80.0f;

	/* The minimum required width of the face region on the depth-map in pixels. Below this value a diagnostic warning will be flagged. */
	UPROPERTY(EditAnywhere, Category = "Processing Diagnostics", meta = (ClampMin = 0.0, ClampMax = 10000.0, EditCondition = "InputType == EDataInputType::DepthFootage", EditConditionHides))
	float MinimumDepthMapFaceWidth = 120.0f;

	/* The maximum allowed percentage difference in stereo baseline between Identity and Performance CaptureData camera calibrations. Above this value a diagnostic warning will be flagged. */
	UPROPERTY(EditAnywhere, Category = "Processing Diagnostics", meta = (ClampMin = 0.0, ClampMax = 100.0, EditCondition = "InputType == EDataInputType::DepthFootage", EditConditionHides))
	float MaximumStereoBaselineDifferenceFromIdentity = 10.0f;

	/* The maximum allowed percentage difference in estimated head scale between Identity and Performance. Above this value a diagnostic warning will be flagged. */
	UPROPERTY(EditAnywhere, Category = "Processing Diagnostics", meta = (ClampMin = 0.0, ClampMax = 100.0, EditCondition = "InputType == EDataInputType::DepthFootage", EditConditionHides))
	float MaximumScaleDifferenceFromIdentity = 7.5f;

	/* Frames that the user has identified which are to be excluded from the processing, eg part of the footage where the face goes out of frame */
	UPROPERTY(EditAnywhere, Category = "Excluded frames")
	TArray<FFrameRange> UserExcludedFrames;

	/* Frames that the processing has identified as producing bad results and should not be exported */
	UPROPERTY(VisibleAnywhere, Category = "Excluded frames")
	TArray<FFrameRange> ProcessingExcludedFrames;

	/** Stores the viewport settings used in the Performance asset editor */
	UPROPERTY()
	TObjectPtr<class UMetaHumanPerformanceViewportSettings> ViewportSettings;

	/** Stores data to extend body tracker functionality */
	UPROPERTY()
	TArray<uint8> AdditionalBodyTrackerData;

	// Export options
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Processing")
	METAHUMANPERFORMANCE_API EStartPipelineErrorType StartPipeline(bool bInIsScriptedProcessing = true);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Processing")
	METAHUMANPERFORMANCE_API void CancelPipeline();

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Processing")
	METAHUMANPERFORMANCE_API bool IsProcessing() const;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Processing")
	METAHUMANPERFORMANCE_API bool CanProcess() const;

	/** Sets the footage capture data. If the footage has an invalid frame rate, the assignment is rejected and FootageCaptureData is set to nullptr. */
	UFUNCTION(BlueprintSetter)
	METAHUMANPERFORMANCE_API void SetFootageCaptureData(UFootageCaptureData* InFootageCaptureData);

	/** Sets the data input type for processing. Clears stale processing state from the previous mode. If the new type requires footage and the current footage has an invalid frame rate, FootageCaptureData is set to nullptr. */
	UFUNCTION(BlueprintSetter)
	METAHUMANPERFORMANCE_API void SetInputType(EDataInputType InInputType);

	/** Sets the audio source. The value is always stored, but processing state is only updated when InputType is Audio. */
	UFUNCTION(BlueprintSetter)
	METAHUMANPERFORMANCE_API void SetAudio(USoundWave* InAudio);

	/** Sets the MetaHuman Identity for this performance. Resets any existing processed output. */
	UFUNCTION(BlueprintSetter)
	METAHUMANPERFORMANCE_API void SetIdentity(UMetaHumanIdentity* InIdentity);

	/**
	 * Sets the processing range. Clamped to the active mode's frame limits, or
	 * held and applied later if no source data is loaded yet. If the request
	 * has no overlap with the limits, snaps to the full extent. Negative
	 * values are treated as zero.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Performance")
	METAHUMANPERFORMANCE_API void SetProcessingRange(int32 InStartFrame, int32 InEndFrame);

	/** Sets the Control Rig asset reference directly. Passing an invalid reference loads the default Control Rig. */
	UFUNCTION(BlueprintSetter)
	METAHUMANPERFORMANCE_API void SetControlRigAssetReference(const FControlRigAssetStrongReference& InControlRigAssetReference);

	/** Negatives clamped to zero; MinDistance clamped to not exceed MaxDistance. */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Performance")
	METAHUMANPERFORMANCE_API void SetDepthDistanceRange(float InMinDistance, float InMaxDistance);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Processing")
	METAHUMANPERFORMANCE_API void SetBlockingProcessing(bool bInBlockingProcessing);

	UFUNCTION(BlueprintSetter)
	METAHUMANPERFORMANCE_API void SetBodyTracking(bool bInBodyTracking);

	// Outputs
	UPROPERTY()
	TArray<FDepthMapDiagnosticsResult> DepthMapDiagnosticResults;

	UPROPERTY()
	float ScaleEstimate = 1.0f;

	// A 64 bit version of Contour Data array to support serialization of longer takes
	TArray64<FFrameTrackingContourData> ContourTrackingResults;

	// A 64 bit version of Animation Data array to support serialization of longer takes
	TArray64<struct FFrameAnimationData> AnimationData;
	
	/** Returns true if there is at least one animation frame with valid data, false otherwise */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|AnimationData")
	UE_DEPRECATED(5.8, "ContainsAnimationData() is deprecated. Use new ContainsAnimationDataType function which takes the data type as a parameter")
	METAHUMANPERFORMANCE_API bool ContainsAnimationData() const;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|AnimationData")
	METAHUMANPERFORMANCE_API bool ContainsAnimationDataType(EFrameAnimationDataType InDataType) const;

	/** Returns animation data (frame numbers are animation frame index not sequencer frame numbers) */
	/** Caller is responsible to ensure data will fit into 32bit TArray */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|AnimationData")
	METAHUMANPERFORMANCE_API TArray<struct FFrameAnimationData> GetAnimationData(int32 InStartFrameNumber = 0, int32 InEndFrameNumber = -1) const;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|AnimationData")
	METAHUMANPERFORMANCE_API int32 GetNumberOfProcessedFrames() const;

	METAHUMANPERFORMANCE_API const TRange<FFrameNumber>& GetProcessingLimitFrameRange() const;
	METAHUMANPERFORMANCE_API const TMap<TWeakObjectPtr<UObject>, TRange<FFrameNumber>>& GetMediaFrameRanges() const;
	METAHUMANPERFORMANCE_API FFrameNumber GetMediaStartFrame() const;
	METAHUMANPERFORMANCE_API TRange<FFrameNumber> GetExportFrameRange(EPerformanceExportRange InExportRange) const;

	int32 GetPipelineStage() const { return PipelineStage; }
	int32 GetPipelineFrameRange() const { return PipelineFrameRangesIndex; }
	int32 GetPipelineFrameRanges() const { return PipelineFrameRanges.Num(); }
	TArray<FFrameRange> GetPipelineFrameRangesArray() const { return PipelineFrameRanges; }

	int32 GetTotalPipelineStage() const;

	/**
	 * Returns the effective Head Skeletal Mesh used for visualization.
	 * If a VisualizationObject if set then this will be that object if its a Skel Mesh, but
	 * if the object is a MetaHuman BP object then the head component of that is returned.
	 * If no VisualizationObject if set the MetaHuman Identity skeletal mesh is returned;
	 * Returns nullptr if no valid Skeletal Mesh was found
	 */
	METAHUMANPERFORMANCE_API USkeletalMesh* GetHeadMesh() const;

	/**
	 * Returns the effective Skeletal Mesh used for visualization.
	 * This will return the VisualizationMesh if valid or the MetaHuman Identity skeletal mesh.
	 * Returns nullptr if no valid Skeletal Mesh was found
	 */

	UE_DEPRECATED(5.8, "This function is deprecated. Please use GetHeadMesh instead.")
	METAHUMANPERFORMANCE_API USkeletalMesh* GetVisualizationMesh() const;

	/** Calculate the head pose from the AnimationData, either using specified reference frame, or auto-selecting the best one */
	METAHUMANPERFORMANCE_API FTransform CalculateReferenceFramePose();

	/** Returns if any frame has valid pose in the AnimationData*/
	METAHUMANPERFORMANCE_API bool HasValidAnimationPose() const;

	/** Returns the first valid pose in the AnimationData*/
	METAHUMANPERFORMANCE_API FTransform GetFirstValidAnimationPose() const;

	/** Returns a list of animation curves used by this Performance */
	METAHUMANPERFORMANCE_API TSet<FString> GetAnimationCurveNames() const;

	/** Returns tooltip text with a reason why processing cant be started */
	METAHUMANPERFORMANCE_API FText GetCannotProcessTooltipText() const;

	/** List of all RGB cameras (views) in the footage capture data */
	TArray<TSharedPtr<FString>> CameraNames;

	METAHUMANPERFORMANCE_API bool HasDepthData() const;

	/** Returns true if depth can be generated from stereo RGB cameras but calibration is missing */
	METAHUMANPERFORMANCE_API bool NeedsCalibrationForDepthGeneration() const;
	UFUNCTION()
	METAHUMANPERFORMANCE_API bool ShouldShowDepthParameters() const;

	/** Returns true if the depth camera location is consistent with the RGB camera location, or diagnostics are not enabled */
	METAHUMANPERFORMANCE_API bool DepthCameraConsistentWithRGBCameraOrDiagnosticsNotEnabled() const;

	METAHUMANPERFORMANCE_API EFrameRangeType GetExcludedFrame(int32 InFrameNumber) const;

	/** Returns a bone position in the reference skeleton - used to account for variations in the heights of different MetaHumans */
	static METAHUMANPERFORMANCE_API FVector GetSkelMeshReferenceBoneLocation(USkeletalMeshComponent* InSkelMeshComponent, const FName& InBoneName);

	/** Estimate the focal length of the footage */
	METAHUMANPERFORMANCE_API bool EstimateFocalLength(FString &OutErrorMessage);

	UE_INTERNAL METAHUMANPERFORMANCE_API FTransform AudioDrivenHeadPoseTransform(const FTransform& InHeadBonePose) const;
	UE_INTERNAL METAHUMANPERFORMANCE_API FTransform AudioDrivenHeadPoseTransformInverse(const FTransform& InRootBonePose) const;

#if WITH_EDITOR
	/** returns true if we have a valid SolverHierarchicalDefinitionsPlusChinCompressConfig, false otherwise (needed for back-compatibility with old configs)*/
	METAHUMANPERFORMANCE_API bool HasSolverHierarchicalDefinitionsPlusChinCompressConfig() const;
#endif

private:
	/** Returns true if the given asset should be filtered out from the Control Rig asset picker. */
	UFUNCTION()
	static bool ShouldFilterControlRigAsset(const FAssetData& InAssetData);

	UFUNCTION()
	bool ShouldFilterVisualizationObject(const FAssetData& InAssetData);

#if WITH_EDITOR
	FOnDataInputTypeChanged OnDataInputTypeChangedDelegate;
	FOnSourceDataChanged OnSourceDataChangedDelegate;
	FOnIdentityChanged OnIdentityChangedDelegate;
	FOnVisualizeMeshChanged OnVisualizeMeshChangedDelegate;
	FOnVisualizeObjectChanged OnVisualizeObjectChangedDelegate;
	FOnSkeletonParamsChanged OnSkeletonParamsChangedDelegate;
	FOnFrameRangeChanged OnFrameRangeChangedDelegate;
	FOnRealtimeAudioChanged OnRealtimeAudioChangedDelegate;
	FOnFrameProcessed OnFrameProcessedDelegate;
	FOnProcessingFinished OnProcessingFinishedDelegate;
	FOnStage1ProcessingFinished OnStage1ProcessingFinishedDelegate;
	FOnStageProcessingFinished OnStageProcessingFinishedDelegate;
	FOnControlRigClassChanged OnControlRigClassChangedDelegate;
	FOnControlRigAssetReferenceChanged OnControlRigAssetReferenceChangedDelegate;
	FOnHeadMovementModeChanged OnHeadMovementModeChangedDelegate;
	FOnHeadMovementReferenceFrameChanged OnHeadMovementReferenceFrameChangedDelegate;
	FOnNeutralPoseCalibrationChanged OnNeutralPoseCalibrationChangedDelegate;
	FOnExcludedFramesChanged OnExcludedFramesChangedDelegate;
	FOnBodyTrackerModeChanged OnBodyTrackerModeChangedDelegate;
	FFrameRangeArrayBuilder::FOnGetCurrentFrame OnGetCurrentFrameDelegate;
	FOnGetHeadMesh OnGetHeadMeshDelegate;

	TArray<TSharedPtr<UE::MetaHuman::Pipeline::FPipeline>> Pipelines;
	TArray<FFrameRange> PipelineFrameRanges;
	TArray<FFrameRange> PipelineExcludedFrames;
	TArray<FFrameRange> RateMatchingExcludedFrames;
	int32 PipelineFrameRangesIndex = 0;
	int32 PipelineStage = 0;
	double PipelineStageStartTime = 0.0;
	FString SolverConfigData;
	FString SolverTemplateData;
	FString SolverDefinitionsData;
	FString SolverHierarchicalDefinitionsData;
	FString SolverHierarchicalDefinitionsPlusChinCompressData;
	TSharedPtr<UE::MetaHuman::Pipeline::FTongueTrackerNode> TongueSolver;
	TSharedPtr<UE::MetaHuman::Pipeline::FSpeechToAnimNode> SpeechToAnimSolver;
	TSharedPtr<UE::MetaHuman::Pipeline::FHyprsenseRealtimeNode> RealtimeMonoSolver;
	TSharedPtr<UE::MetaHuman::Pipeline::FRealtimeSpeechToAnimNode> RealtimeSpeechToAnimSolver;

	TSharedPtr<IMetaHumanBodyTrackerInterface::FBodyTrackerDataInterface> BodyTrackerData;
	int32 BodyTrackerFinalPipelineStage = 0;

	METAHUMANPERFORMANCE_API void StartPipelineStage();
	METAHUMANPERFORMANCE_API void SendTelemetryForProcessFootageRequest(TSharedPtr<const UE::MetaHuman::Pipeline::FPipelineData> InPipelineData);

	FString TrackingResultsPinName;
	FString AnimationResultsPinName;
	FString DepthMapDiagnosticsResultsPinName;
	FString ScaleDiagnosticsResultsPinName;

	METAHUMANPERFORMANCE_API void FrameComplete(TSharedPtr<UE::MetaHuman::Pipeline::FPipelineData> InPipelineData);
	METAHUMANPERFORMANCE_API void ProcessComplete(TSharedPtr<UE::MetaHuman::Pipeline::FPipelineData> InPipelineData);

	METAHUMANPERFORMANCE_API void AddSpeechToAnimSolveToPipeline(UE::MetaHuman::Pipeline::FPipeline& InPipeline, TSharedPtr<UE::MetaHuman::Pipeline::FSpeechToAnimNode> InSpeechAnimNode, FString& OutAnimationResultsPinName);
	METAHUMANPERFORMANCE_API void AddTongueSolveToPipeline(UE::MetaHuman::Pipeline::FPipeline& InPipeline, TSharedPtr<UE::MetaHuman::Pipeline::FSpeechToAnimNode> InTongueSolveNode, TSharedPtr<UE::MetaHuman::Pipeline::FNode> InInputNode, TSharedPtr<UE::MetaHuman::Pipeline::FDropFrameNode> InDropFrameNode, TSharedPtr<UE::MetaHuman::Pipeline::FNode>& OutAnimationResultsNode, FString& OutAnimationResultsPinName);
#endif

	METAHUMANPERFORMANCE_API void ResetOutput(bool bInWholeSequence);

	bool bBlockingProcessing = false;

	/** Only one performance asset can be processed at the time */
	static METAHUMANPERFORMANCE_API TWeakObjectPtr<UMetaHumanPerformance> CurrentlyProcessedPerformance;

	METAHUMANPERFORMANCE_API void UpdateFrameRanges();
	METAHUMANPERFORMANCE_API float CalculateAudioProcessingOffset();

	TMap<TWeakObjectPtr<UObject>, TRange<FFrameNumber>> MediaFrameRanges;
	TRange<FFrameNumber> ProcessingLimitFrameRange = TRange<FFrameNumber>(0, 0);

	METAHUMANPERFORMANCE_API void LoadDefaultTracker();
	METAHUMANPERFORMANCE_API void LoadDefaultSolver();
	METAHUMANPERFORMANCE_API void LoadDefaultControlRig();

	METAHUMANPERFORMANCE_API void UpdateCaptureDataConfigName();
	METAHUMANPERFORMANCE_API void OnCaptureDataInternalsChanged();

	// Wrappers around AddUObject / RemoveAll that keep SubscribedFootageCaptureData
	// in sync with the real registration.
	void SubscribeFootageObserver(UFootageCaptureData* InFootage);
	void UnsubscribeFootageObserver(UFootageCaptureData* InFootage);

	// The transaction system can roll back FootageCaptureData but cannot reverse
	// the delegate registration on the other UObject; this mirror lets
	// PostEditUndo detect and fix the divergence.
	TWeakObjectPtr<UFootageCaptureData> SubscribedFootageCaptureData;

	METAHUMANPERFORMANCE_API TArray<UE::MetaHuman::FSequencedImageTrackInfo> CreateSequencedImageTrackInfos();
	METAHUMANPERFORMANCE_API bool FootageCaptureDataViewLookupsAreValid() const;

	ETimecodeAlignment PreviousTimecodeAlignment = ETimecodeAlignment::None;

	UPROPERTY(Transient)
	TObjectPtr<UFootageCaptureData> PreviousFootageCaptureData;

	UPROPERTY(Transient)
	TObjectPtr<UMetaHumanFaceAnimationSolver> PreviousDefaultSolver;

	// SetProcessingRange may be called before source data is loaded. The request
	// is held here and applied by the next ApplySourceDataChange that runs against
	// valid limits.
	UPROPERTY(Transient)
	bool bHasPendingProcessingRange = false;
	UPROPERTY(Transient)
	uint32 PendingStartFrameToProcess = 0;
	UPROPERTY(Transient)
	uint32 PendingEndFrameToProcess = 0;

	UPROPERTY()
	TArray<FFrameTrackingContourData> ContourTrackingResults_DEPRECATED;

	UPROPERTY()
	TArray<struct FFrameAnimationData> AnimationData_DEPRECATED;

	UPROPERTY()
	TObjectPtr<class USkeletalMesh> OverrideVisualizationMesh_DEPRECATED;

	bool bMetaHumanAuthoringObjectsPresent = false;

	bool bIsScriptedProcessing = false;
	double ProcessingStartTime = 0.f;

	FString EstimateFocalLengthErrorMessage;
	bool bEstimateFocalLengthOK = false;
	METAHUMANPERFORMANCE_API void EstimateFocalLengthFrameComplete(TSharedPtr<UE::MetaHuman::Pipeline::FPipelineData> InPipelineData);
	METAHUMANPERFORMANCE_API void EstimateFocalLengthProcessComplete(TSharedPtr<UE::MetaHuman::Pipeline::FPipelineData> InPipelineData);

	METAHUMANPERFORMANCE_API bool HasFrameRateNominatorEqualZero();

	// Sentinel for "no source data is loaded yet" - the active source yields no
	// usable frame range.
	bool HasEmptyProcessingLimits() const;

	void ApplySourceDataChange();

	void SetFaceTrackingModels();

	// Rotation and push back needed so things appear correctly in the viewport
	const FTransform AudioDrivenAnimationViewportTransform = FTransform(FRotator(0, 90, 0), FVector(40.0, 0.0, 0.0));
};
