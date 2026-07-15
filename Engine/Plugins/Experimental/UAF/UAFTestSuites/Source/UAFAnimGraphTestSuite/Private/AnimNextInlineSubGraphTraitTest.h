// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraitCore/TraitSharedData.h"
#include "AnimNextInlineSubGraphTraitTest.generated.h"

USTRUCT()
struct FInlineSubGraphTest_LeafTraitSharedData : public FAnimNextTraitSharedData
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Value = 0;
};
