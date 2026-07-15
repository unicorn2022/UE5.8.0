// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanBodyAnimRetargeter.h"

#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetProcessor.h"
#include "Retargeter/IKRetargetProfile.h"
#include "Retargeter/IKRetargetOps.h"
#include "Engine/SkeletalMesh.h"
#include "Algo/AnyOf.h"

DEFINE_LOG_CATEGORY_STATIC(LogMetaHumanBodyAnimRetargeter, Log, All);

bool FMetaHumanBodyAnimRetargeter::Initialize(
	const USkeletalMesh* InSourceMesh,
	const USkeletalMesh* InTargetMesh,
	const UIKRetargeter* InRetargeter)
{
	check(IsInGameThread());

	if (!InSourceMesh || !InTargetMesh || !InRetargeter)
	{
		UE_LOGF(LogMetaHumanBodyAnimRetargeter, Error, "Can't initialize the retargeter with null skeletal mesh or null retargeter.");
		return false;
	}

	SourceMesh.Reset(InSourceMesh);
	TargetMesh.Reset(InTargetMesh);

	FRetargetInitParameters InitParams;
	InitParams.SourceSkeletalMesh = SourceMesh.Get();
	InitParams.TargetSkeletalMesh = TargetMesh.Get();
	InitParams.RetargeterAsset = InRetargeter;
	Processor.Initialize(InitParams);

	if (!Processor.IsInitialized())
	{
		UE_LOGF(LogMetaHumanBodyAnimRetargeter, Error, "Retargeter processor failed to initialize.");
		return false;
	}

	// Populate the retarget profile from the asset so per-chain settings (rotation/translation alphas, etc.) apply at runtime. Matches IKRetargetBatchOperation::RunRetarget.
	Profile = FRetargetProfile();
	Profile.FillProfileWithAssetSettings(InRetargeter);

	if (Algo::AnyOf(Processor.GetRetargetOps(), [](const FInstancedStruct& OpStruct)
		{
			const FIKRetargetOpBase& Op = OpStruct.Get<FIKRetargetOpBase>();
			return Op.IsEnabled() && !Op.IsInitialized();
		}))
	{
		UE_LOGF(LogMetaHumanBodyAnimRetargeter, Warning, "Retargeter '%ls' is not fully configured and may produce incorrect animation", *InRetargeter->GetName());
	}

	return true;
}

void FMetaHumanBodyAnimRetargeter::RetargetFrame(
	const TMap<FString, FTransform>& InBodyAnimationData,
	TMap<FString, FTransform>& OutBodyAnimationData,
	float InDeltaTime)
{
	check(IsInGameThread());

	if (!Processor.IsInitialized())
	{
		return;
	}

	const FReferenceSkeleton& SourceRefSkeleton = SourceMesh->GetRefSkeleton();
	TArray<FTransform> SourceGlobalPose = SourceRefSkeleton.GetRefBonePose();

	for (int32 BoneIndex = 0; BoneIndex < SourceRefSkeleton.GetNum(); ++BoneIndex)
	{
		const FString BoneName = SourceRefSkeleton.GetBoneName(BoneIndex).ToString();
		const FTransform* LocalTransform = InBodyAnimationData.Find(BoneName);
		if (!LocalTransform)
		{	
			const int32 ParentIndex = SourceRefSkeleton.GetParentIndex(BoneIndex);
			if (ParentIndex != INDEX_NONE)
			{
				SourceGlobalPose[BoneIndex] = SourceGlobalPose[BoneIndex] * SourceGlobalPose[ParentIndex];
			}	
		}
		else
		{
			const int32 ParentIndex = SourceRefSkeleton.GetParentIndex(BoneIndex);
			if (ParentIndex == INDEX_NONE)
			{
				SourceGlobalPose[BoneIndex] = *LocalTransform;
			}
			else
			{
				SourceGlobalPose[BoneIndex] = *LocalTransform * SourceGlobalPose[ParentIndex];
			}
		}
	}

	// Strip scale from component-space pose (matches editor batch retarget)
	for (FTransform& Transform : SourceGlobalPose)
	{
		Transform.SetScale3D(FVector::OneVector);
	}

	// Apply source scale factor if any (matches editor batch retarget)
	Processor.ApplySourceScaleToPose(SourceGlobalPose);

	FRetargetRunParameters RunParams;
	RunParams.SourceGlobalPose = &SourceGlobalPose;
	RunParams.Profile = &Profile;
	RunParams.DeltaTime = InDeltaTime;
	const TArray<FTransform>& TargetGlobalPose = Processor.RunRetargeter(RunParams);

	const FReferenceSkeleton& TargetRefSkeleton = TargetMesh->GetRefSkeleton();

	OutBodyAnimationData.Reset();
	OutBodyAnimationData.Reserve(TargetRefSkeleton.GetNum());

	for (int32 BoneIndex = 0; BoneIndex < TargetRefSkeleton.GetNum(); ++BoneIndex)
	{
		const FString BoneName = TargetRefSkeleton.GetBoneName(BoneIndex).ToString();
		const int32 ParentIndex = TargetRefSkeleton.GetParentIndex(BoneIndex);

		if (ParentIndex == INDEX_NONE)
		{
			OutBodyAnimationData.Add(BoneName, TargetGlobalPose[BoneIndex]);
		}
		else
		{
			OutBodyAnimationData.Add(BoneName, TargetGlobalPose[BoneIndex].GetRelativeTransform(TargetGlobalPose[ParentIndex]));
		}
	}
}
