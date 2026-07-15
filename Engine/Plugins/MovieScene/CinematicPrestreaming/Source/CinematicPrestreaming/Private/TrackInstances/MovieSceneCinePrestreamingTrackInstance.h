// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/TrackInstance/MovieSceneTrackInstance.h"
#include "MovieSceneCinePrestreamingTrackInstance.generated.h"

UCLASS()
class UMovieSceneCinePrestreamingTrackInstance : public UMovieSceneTrackInstance
{
	GENERATED_BODY()

protected:
	/** UMovieSceneTrackInstance interface */
	void OnInputAdded(const FMovieSceneTrackInstanceInput& InInput) override;
	void OnInputRemoved(const FMovieSceneTrackInstanceInput& InInput) override;
	void OnAnimate() override;
};
