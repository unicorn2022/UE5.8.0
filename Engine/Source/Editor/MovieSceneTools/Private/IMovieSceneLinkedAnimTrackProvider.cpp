// Copyright Epic Games, Inc. All Rights Reserved.

#include "IMovieSceneLinkedAnimTrackProvider.h"

FName IMovieSceneLinkedAnimTrackProvider::GetModularFeatureName()
{
	static FName FeatureName = FName(TEXT("MovieSceneLinkedAnimTrackProvider"));
	return FeatureName;
}
