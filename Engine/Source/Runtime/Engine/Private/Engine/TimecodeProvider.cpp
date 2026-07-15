// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/TimecodeProvider.h"
#include "Engine/Engine.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TimecodeProvider)

namespace UE::Time
{
	static FAutoConsoleCommand TimecodeProviderReinitializeCmd(
		TEXT("TimecodeProvider.Reinitialize"),
		TEXT("Reinitialize the current timecode provider"),
		FConsoleCommandDelegate::CreateLambda([]()
			{
				if (GEngine)
				{
					GEngine->ReinitializeTimecodeProvider();
				}
			})
	);
}


FQualifiedFrameTime UTimecodeProvider::GetDelayedQualifiedFrameTime() const
{
	FQualifiedFrameTime NewFrameTime = GetQualifiedFrameTime();
	NewFrameTime.Time -= FFrameTime::FromDecimal(FrameDelay);
	return NewFrameTime;
}


FTimecode UTimecodeProvider::GetTimecode() const
{
	return GetQualifiedFrameTime().ToTimecode();
}


FTimecode UTimecodeProvider::GetDelayedTimecode() const
{
	return GetDelayedQualifiedFrameTime().ToTimecode();
}

