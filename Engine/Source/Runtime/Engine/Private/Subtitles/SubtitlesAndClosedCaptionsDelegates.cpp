// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subtitles/SubtitlesAndClosedCaptionsDelegates.h"
#include "Subtitles/SubtitlesAndClosedCaptionsTypes.h"

TDelegate<void(FQueueSubtitleParameters, const ESubtitleTiming)> FSubtitlesAndClosedCaptionsDelegates::QueueSubtitle;
TDelegate<void(const FName&, const FName&, const float), FDefaultTSDelegateUserPolicy> FSubtitlesAndClosedCaptionsDelegates::QueueSubtitleFromSoundWaveName;
TDelegate<bool(FSubtitleAssetData)> FSubtitlesAndClosedCaptionsDelegates::IsSubtitleActive;
TDelegate<void(FSubtitleAssetData)> FSubtitlesAndClosedCaptionsDelegates::StopSubtitle;
TDelegate<void()> FSubtitlesAndClosedCaptionsDelegates::StopAllSubtitles;