// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkHubPlaybackSourceSettings.h"

#include "LiveLinkHubPlaybackSourceFactory.h"
#include "LiveLinkSourceFactory.h"
#include "LiveLinkSubjectSettings.h"


ULiveLinkHubPlaybackSourceSettings::ULiveLinkHubPlaybackSourceSettings()
{
	Factory = ULiveLinkHubPlaybackSourceFactory::StaticClass();
}

FText ULiveLinkHubPlaybackSourceSettings::GetSourceNameOverride(ULiveLinkSubjectSettings* SubjectSettings, FText SourceType)
{
	check(SubjectSettings);

	if (!SubjectSettings->OriginalSourceName.IsNone())
	{
		return FText::FromName(SubjectSettings->OriginalSourceName);
	}

	return SourceType;
}
