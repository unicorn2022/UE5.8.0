// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Features/IModularFeature.h"
#include "Pipeline/Pipeline.h"
#include "GameFramework/Actor.h"
#include "FrameRange.h"
#include "Templates/SubclassOf.h"

#if WITH_EDITOR
#include "DetailLayoutBuilder.h"
#endif

#include "MetaHumanBodyTrackerInterface.generated.h"

#define UE_API METAHUMANBODYTRACKERINTERFACE_API



UENUM()
enum class EMetaHumanBodyTrackerMode : uint8
{
	None = 0,
	Realtime,
	Offline
};

UCLASS()
class UE_API AMetaHumanBodyDriverActorInterface : public AActor
{
	GENERATED_BODY()

public:

	virtual void Initialize() { }

	virtual void Update(const FFrameAnimationData& InAnimationData) { }

	virtual void SetBodyDriverSkeletalMeshComponent(TWeakObjectPtr<USkeletalMeshComponent> InSkeletalMeshComponent) { }

	virtual FVector GetDebugRelativeLocation() const { return FVector::ZeroVector; }
	virtual FRotator GetDebugRelativeRotation() const { return FRotator::ZeroRotator; }
	virtual FVector GetCameraRelativeLocation() const { return FVector::ZeroVector; }

	virtual void SetVisualizationSkeletalMeshParams(bool bInVisible, const FVector& InOffset, const FColor& InColor) { }

	UPROPERTY()
	TObjectPtr<class USkeletalMesh> SMPLXSkeletalMeshSkinned;

	UPROPERTY()
	TObjectPtr<class USkeletalMesh> MHSkeletalMeshSizedBare;

	UPROPERTY()
	TObjectPtr<class USkeletalMesh> BodyDriverSkeletalMesh;
};

class IMetaHumanBodyTrackerInterface : public IModularFeature
{
public:

	virtual ~IMetaHumanBodyTrackerInterface() = default;

	static FName GetModularFeatureName()
	{
		static const FName ModularFeatureName = FName(TEXT("MetaHumanBodyTrackerInterface"));
		return ModularFeatureName;
	}

	class FBodyTrackerDataInterface
	{
	};

	struct FBodyTrackerInputParams
	{
		EMetaHumanBodyTrackerMode BodyTrackerMode = EMetaHumanBodyTrackerMode::None;

		int32 PipelineStage = -1;
		int32 PipelineFrameRangesIndex = -1;
		TArray<FFrameRange> PipelineFrameRanges;
		TArray<FFrameRange> PipelineExcludedFrames;

		TSharedPtr<UE::MetaHuman::Pipeline::FNode> ImageSrcNode;
		TSharedPtr<UE::MetaHuman::Pipeline::FNode> FaceAnimSrcNode;

		TArray64<FFrameAnimationData> AnimationData;

		FFrameRange ImageSequenceRange;

		bool bSkipPreview = false;
		float Fps = 30.0f;
		bool bAutoBodyHeight = true;
		float BodyHeight = 180.0f;
		class UMetaHumanPerformance* Performance = nullptr;
		bool bEnableFootLocking = true;

		TSharedPtr<FBodyTrackerDataInterface> BodyTrackerData;
	};

	struct FBodyTrackerOutputParams
	{
		FString AnimationPinName;
		TSharedPtr<FBodyTrackerDataInterface> BodyTrackerData;
		int32 BodyTrackerFinalPipelineStage = 0;
	};

	virtual bool ExtendPipeline(const FBodyTrackerInputParams& InBodyTrackerInputParams, UE::MetaHuman::Pipeline::FPipeline& InOutPipeline, FBodyTrackerOutputParams& OutBodyTrackerOutputParams) const = 0;

	virtual TSubclassOf<AMetaHumanBodyDriverActorInterface> GetBodyDriverActorClass() const = 0;

	virtual FString GetBodyControlRigAssetPath() const = 0;

	virtual FString GetMetaHumanRetargeterAssetPath() const = 0;

	virtual USkeletalMesh* MakeMHSkeletalMesh(UObject* InOuter, const TArray<float>& InShape) const = 0;

	virtual USkeletalMesh* MakeSMPLSkeletalMesh(UObject* InOuter, const TArray<float>& InShape) const = 0;

#if WITH_EDITOR
	virtual void CustomizePerformanceDetails(IDetailLayoutBuilder& InDetailBuilder) const = 0;
#endif
};

#undef UE_API
