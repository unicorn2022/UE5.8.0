// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SpeechAnimationSolverTypes.h"

#include "Widgets/SCompoundWidget.h"

#if WITH_EDITOR
#include "PropertyHandle.h"
#endif

#define UE_API METAHUMANCORETECH_API



#if WITH_EDITOR
class SAudioDrivenAnimationMood : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SAudioDrivenAnimationMood) {}
	SLATE_END_ARGS()

	UE_API void Construct(const FArguments& InArgs, bool bInOffline, TSharedRef<IPropertyHandle> InMoodPropertyHandle); // bInOffline is no longer used, but kept for backcompatability
};
#endif

#undef UE_API
