// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"
#include "MaterialBridge/Slot/AvaMaterialBridgeFeature.h"
#include "AvaMaterialBridgeBlendModeFeature.generated.h"

/** Feature representing the blend mode to configure for the slot */
USTRUCT()
struct FAvaMaterialBridgeBlendModeFeature : public FAvaMaterialBridgeFeature
{
	GENERATED_BODY()

	/** The desired blend mode for the slot */
	UPROPERTY()
	TEnumAsByte<EBlendMode> BlendMode = EBlendMode::BLEND_Opaque;
};
