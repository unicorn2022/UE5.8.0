// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraitCore/TraitHandle.h"
#include "TraitCore/TraitSharedData.h"

#include "PassthroughTraitData.generated.h"

/** A trait that passes through the input without modification. */
USTRUCT(meta = (DisplayName = "Passthrough", ShowTooltip=true))
struct FAnimNextPassthroughSharedData : public FAnimNextTraitSharedData
{
	GENERATED_BODY()

	/** Input to pass to output */
	UPROPERTY()
	FAnimNextTraitHandle Input;
};