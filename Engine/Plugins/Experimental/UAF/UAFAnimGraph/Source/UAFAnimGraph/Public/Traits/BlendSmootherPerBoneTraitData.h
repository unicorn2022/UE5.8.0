// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraitCore/TraitSharedData.h"

#include "BlendSmootherPerBoneTraitData.generated.h"

/** An additive trait that smoothly blends with per-bone weights. */
USTRUCT(meta = (DisplayName = "Blend Smoother Per Bone Core", ShowTooltip=true))
struct FAnimNextBlendSmootherPerBoneCoreTraitSharedData : public FAnimNextTraitSharedData
{
	GENERATED_BODY()
};