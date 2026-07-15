// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Systems/MovieScenePropertySystem.h"

#include "MovieSceneActorReferencePropertySystem.generated.h"

UCLASS(MinimalAPI)
class UMovieSceneActorReferencePropertySystem : public UMovieScenePropertySystem
{
	GENERATED_BODY()

	UMovieSceneActorReferencePropertySystem(const FObjectInitializer& ObjInit);

	MOVIESCENETRACKS_API virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;
};

