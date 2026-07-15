// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/ObjectMacros.h"

#include "CompositeCoreTypes.generated.h"

#define UE_API COMPOSITECORE_API

/* Holdout property management on registered primitives. */
UENUM(BlueprintType)
enum class ECompositeCoreHoldoutManagement : uint8
{
	None,
	Automatic,
};

#undef UE_API
