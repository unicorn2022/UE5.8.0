// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanMassAnimDesc.generated.h"

/**
 * Native struct used to pipe data from Mass Instance -> Actor -> Anim Blueprint system.
 */
USTRUCT(BlueprintType)
struct FMetahumanMassAnimDesc
{
	GENERATED_BODY()

	// @TODO: Populate this from ISKM transform provider data
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mass)
	TObjectPtr<class UAnimSequence> AnimSequence = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mass)
	int32 AnimSequenceIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mass)
	float Position = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mass)
	float Significance = 0.0f;

	// This can be used by animation to fire reset events and all kinds of other logic that's needed for a clean swap.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mass)
	bool bJustSwapped = false;
};
