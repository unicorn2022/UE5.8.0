// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "MetaHumanCrowdAnimationConfig.generated.h"

class UAnimSequence;

USTRUCT(BlueprintType)
struct FMetaHumanCrowdBakeAnimationData
{
	GENERATED_BODY()

public:
	/** Name used to identify this animation entry. Matches FMetaHumanCrowdAnimationAssemblyOutput::Name. */
	UPROPERTY(EditAnywhere, Category = "Crowd")
	FName Name;

	/** Toggles the animation input mode */
	UPROPERTY(EditAnywhere, Category = "Crowd")
	bool bUseMergedAnimation = false;

	/** Optional source animation to bake through the face mesh post-process ABP (RigLogic). */
	UPROPERTY(EditAnywhere, Category = "Crowd", meta = (EditCondition = "!bUseMergedAnimation", EditConditionHides))
	TObjectPtr<UAnimSequence> FaceAnimSequence;

	/** Source animation for the body. Bone tracks are sampled directly and merged with the baked face animation. */
	UPROPERTY(EditAnywhere, Category = "Crowd", meta = (EditCondition = "!bUseMergedAnimation", EditConditionHides))
	TObjectPtr<UAnimSequence> BodyAnimSequence;

	/** Merged animation, expected to contain body animation and face animation curves (optional). */
	UPROPERTY(EditAnywhere, Category = "Crowd", meta = (EditCondition = "bUseMergedAnimation", EditConditionHides))
	TObjectPtr<UAnimSequence> MergedAnimSequence;

	/** Whether to set the output animation sequence asset to loop by default */
	UPROPERTY(EditAnywhere, Category = "Crowd")
	bool bLoop = true;

	/** Whether to set the root motion flags */
	UPROPERTY(EditAnywhere, Category = "Crowd")
	bool bRootMotion = true;

	bool IsValid() const
	{
		if (Name.IsNone())
		{
			return false;
		}

		if (bUseMergedAnimation)
		{
			return MergedAnimSequence != nullptr;
		}
		else
		{
			return BodyAnimSequence != nullptr;
		}
	}
};

UCLASS(BlueprintType)
class METAHUMANCROWDEDITOR_API UMetaHumanCrowdAnimationConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * When merging baked face and body animations, only face bone tracks that are children of this
	 * bone (inclusive) are taken from the baked face animation. All other bones come from the body
	 * animation. Defaults to "head", which is the typical choice for MetaHuman face rigs.
	 * Set to None to use all bones from the face bake with no filtering.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animations")
	FName FaceRootBoneName = FName("head");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animations")
	TArray<FMetaHumanCrowdBakeAnimationData> AnimationsToBake;
};