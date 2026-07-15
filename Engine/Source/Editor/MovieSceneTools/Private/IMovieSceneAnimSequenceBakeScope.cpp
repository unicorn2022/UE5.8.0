// Copyright Epic Games, Inc. All Rights Reserved.

#include "IMovieSceneAnimSequenceBakeScope.h"

FName IMovieSceneAnimSequenceBakeScope::GetModularFeatureName()
{
	static const FName Name(TEXT("MovieSceneAnimSequenceBakeScope"));
	return Name;
}
