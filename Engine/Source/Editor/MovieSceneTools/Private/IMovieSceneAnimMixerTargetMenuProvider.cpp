// Copyright Epic Games, Inc. All Rights Reserved.

#include "IMovieSceneAnimMixerTargetMenuProvider.h"

FName IMovieSceneAnimMixerTargetMenuProvider::GetModularFeatureName()
{
	static FName FeatureName(TEXT("MovieSceneAnimMixerTargetProvider"));
	return FeatureName;
}
