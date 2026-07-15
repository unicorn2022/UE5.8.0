// Copyright Epic Games, Inc. All Rights Reserved.

#include "IMovieSceneAnimMixerItemMenuProvider.h"

IMovieSceneAnimMixerItemMenuProvider::IMovieSceneAnimMixerItemMenuProvider()
{

}

IMovieSceneAnimMixerItemMenuProvider::~IMovieSceneAnimMixerItemMenuProvider(){}

FName IMovieSceneAnimMixerItemMenuProvider::GetModularFeatureName()
{
	static FName FeatureName = FName(TEXT("MovieSceneAnimMixerItemProvider"));
	return FeatureName;
}