// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HyprsenseUtils.h"

#include "NNETypes.h"
#include "Pipeline/Node.h"
#include "Pipeline/PipelineData.h"
#include "HAL/ThreadSafeBool.h"
#include "HyprsenseRealtimeNode.generated.h"

#define UE_API METAHUMANPIPELINECORE_API

namespace UE::NNE
{
	class IModelInstanceRunSync;	
}

UENUM()
enum class EHyprsenseRealtimeNodeDebugImage : uint8
{
	None = 0,
	Input UMETA(DisplayName = "Input Video"),
	FaceDetect,
	Headpose,
	Trackers,
	Solver
};

UENUM()
enum class EHyprsenseRealtimeNodeState : uint8
{
	Unknown = 0,
	OK,
	NoFace,
	SubjectTooFar,
};

/** How to handle face frames where head rotation exceeds the allowed yaw or pitch. */
UENUM()
enum class EFaceUnsolvedFrameBehavior : uint8
{
	/** Leave the frame empty. Exported curves interpolate across the gap based on the curve interpolation setting. */
	None         UMETA(DisplayName = "None"),
	/** Fill the frame with a neutral face pose. */
	NeutralPose  UMETA(DisplayName = "Neutral Pose")
};

UENUM()
enum class EHyprsenseRealtimeNodeModelConfiguration : uint8
{
	Default = 0,
	StereoHMC,
	SmallFace
};

USTRUCT(BlueprintType)
struct FMonocularAnimationPipelineModels
{
	GENERATED_BODY()

	UE_API FMonocularAnimationPipelineModels();

	// set the model configuration for the realtime animation pipeline models
	UE_API void SetModelConfiguration(EHyprsenseRealtimeNodeModelConfiguration InModelConfiguration);

	// The NNE runtime backend to use for inference (e.g. "NNERuntimeCoreML", "NNERuntimeORTCpu")
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Properties, meta = (DisplayName = "NNE Backend"))
	FString NNEBackend;

	// The model which will be used for initial face detection
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Properties, meta = (AllowedClasses = "/Script/NNE.NNEModelData"))
	FSoftObjectPath FaceDetector;

	// The model which will be used for face headpose calculation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Properties, meta = (AllowedClasses = "/Script/NNE.NNEModelData"))
	FSoftObjectPath FaceHeadPoseTracker;

	// The model which will be used for solving
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Properties, meta = (AllowedClasses = "/Script/NNE.NNEModelData"))
	FSoftObjectPath FaceSolver;

	// are all model paths set?
	UE_API bool IsValid() const;

private:
	// The model configuration — cached so we can re-apply defaults when the backend changes
	EHyprsenseRealtimeNodeModelConfiguration ModelConfiguration = EHyprsenseRealtimeNodeModelConfiguration::Default;
};


namespace UE::MetaHuman::Pipeline
{

// Model output variants — each backend (ONNX, CoreML) may use different tensor names
// and shapes for the same logical outputs. Resolved at load time in CheckModels.
//
// An FModelVariant maps logical output roles (e.g. "Scores", "Landmarks") to tensor
// descriptors. The role keys are constant across backends; the tensor names and shapes
// within each FModelOutputDesc may differ between ONNX, CoreML, etc.

struct FModelOutputDesc
{
	FString Name;
	UE::NNE::FTensorShape Shape;

	// Solver variants only. For compact outputs where only a subset of controls are
	// active, maps each position in the full layout to whether it is present in the
	// compact output. Empty means all controls are present (no expansion needed).
	TConstArrayView<const bool> ActiveControlFlags;
};

using FModelVariant = TMap<FString, FModelOutputDesc>;

class FHyprsenseRealtimeNode : public FNode, public FHyprsenseUtils
{
public:

	UE_API FHyprsenseRealtimeNode(const FString& InName);

	UE_API void SetModels(const FMonocularAnimationPipelineModels& InModels);

	UE_API virtual bool Start(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;

	UE_API bool LoadModels();

	enum ErrorCode
	{
		FailedToInitialize = 0,
		FailedToDetect,
		FailedToTrack,
		FailedToSolve
	};

	UE_API void SetDebugImage(EHyprsenseRealtimeNodeDebugImage InDebugImage);
	UE_API EHyprsenseRealtimeNodeDebugImage GetDebugImage();

	UE_API void SetFocalLength(float InFocalLength);
	UE_API float GetFocalLength();

	UE_API void SetHeadStabilization(bool bInHeadStabilization);
	UE_API bool GetHeadStabilization() const;

	UE_API void SetHeadAllowedRotation(bool bInEnabled, float InLeftRight, float InUpDown);
	UE_API void SetHeadRotationErrorHandler(EFaceUnsolvedFrameBehavior InHandler);

	UE_API void SetNNEBackend(const FString& InNNEBackend);

	UE_API FString GetNNEBackend() const;

private:


	struct FModelValidationResult
	{
		// Matched variant indices into ModelVariants::Detector/Headpose/Solver tables
		int32 DetectorVariantIndex = INDEX_NONE;
		int32 HeadposeVariantIndex = INDEX_NONE;
		int32 SolverVariantIndex = INDEX_NONE;

		// FaceSolver matched input size (512x256 standard or 256x128 small-faces)
		uint32 SolverInputSizeY = 0;
		uint32 SolverInputSizeX = 0;
	};


	EHyprsenseRealtimeNodeDebugImage DebugImage = EHyprsenseRealtimeNodeDebugImage::None;
	FCriticalSection DebugImageMutex;

	float FocalLength = -1;
	FCriticalSection FocalLengthMutex;

	FThreadSafeBool bHeadStabilization = true;

	TSharedPtr<UE::NNE::IModelInstanceRunSync> FaceDetector = nullptr;
	TSharedPtr<UE::NNE::IModelInstanceRunSync> Headpose = nullptr;
	TSharedPtr<UE::NNE::IModelInstanceRunSync> Solver = nullptr;

	// Resolved tensor names and output index maps — populated once in LoadModels.
	// Each model variant (ONNX, CoreML) may use different names for the same logical outputs.
	// The index maps allow Process() to place output buffers at the correct position without
	// per-frame lookups.
	FModelVariant DetectorModelVariant;
	FModelVariant HeadposeModelVariant;
	FModelVariant SolverModelVariant;
	TMap<FString, int32> DetectorOutputIndices;
	TMap<FString, int32> HeadposeOutputIndices;
	TMap<FString, int32> SolverOutputIndices;

	const uint32 HeadposeInputSizeX = 256;
	const uint32 HeadposeInputSizeY = 256;

	// size of solver input can vary
	uint32 SolverInputSizeX = 256;
	uint32 SolverInputSizeY = 512;

	// number of face detector outputs can vary
	uint32 DetectorOutputCount = 4212;

	bool bFaceDetected = false;
	TArray<FVector2D> TrackingPoints;
	FVector HeadTranslation = FVector::ZeroVector;

	bool bApplyAngleLimits = false;

	const float FaceScoreThreshold = 0.5;
	float LeftRightHeadRotationThreshold = 45;
	float UpDownHeadRotationThreshold = 30;
	EFaceUnsolvedFrameBehavior HeadRotationErrorHandler = EFaceUnsolvedFrameBehavior::None;

	const float LandmarkAwareSmoothingThresholdInCm = 1.5f;
	TArray<FVector2D> PreviousTrackingPoints;
	FTransform PreviousTransform;
	UE_API FTransform LandmarkAwareSmooth(const TArray<FVector2D>& InTrackingPoints, const FTransform& InTransform, const float InFocalLength);

	UE_API Matrix23f GetTransformFromPoints(const TArray<FVector2D>& InPoints, const FVector2D& InSize, bool bInIsStableBox, Matrix23f& OutTransformInv) const;

	UE_API bool CheckModels(TSharedPtr<UE::NNE::IModelInstanceRunSync> InDetector, TSharedPtr<UE::NNE::IModelInstanceRunSync> InHeadpose, TSharedPtr<UE::NNE::IModelInstanceRunSync> InSolver, FModelValidationResult& OutValidationResult) const;

	FMonocularAnimationPipelineModels MonocularAnimationPipelineModels;
};

}

#undef UE_API
