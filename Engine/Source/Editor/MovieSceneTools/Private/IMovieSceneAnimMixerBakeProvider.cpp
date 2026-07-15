// Copyright Epic Games, Inc. All Rights Reserved.

#include "IMovieSceneAnimMixerBakeProvider.h"

FName IMovieSceneAnimMixerBakeProvider::GetModularFeatureName()
{
	static FName FeatureName(TEXT("MovieSceneAnimMixerBakeProvider"));
	return FeatureName;
}
