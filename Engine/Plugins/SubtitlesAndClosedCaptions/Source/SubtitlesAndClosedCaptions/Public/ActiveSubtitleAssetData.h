// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

struct FSubtitleAssetData;

#include "Engine/TimerHandle.h"
#include "Subtitles/SubtitlesAndClosedCaptionsTypes.h"
#include "UObject/ObjectPtr.h"

#include "ActiveSubtitleAssetData.generated.h"

USTRUCT()
struct FActiveSubtitleAssetData
{
	GENERATED_BODY()

	UPROPERTY()
	FSubtitleAssetData Subtitle;

	FTimerHandle DurationTimerHandle;
};
