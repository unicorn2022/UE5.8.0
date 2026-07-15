// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneStateEventTemplate.h"
#include "UObject/Object.h"
#include "SceneStateDebugControlsObject.generated.h"

class USceneStateObject;

/** Temporary object used for debug controls */
UCLASS(Transient)
class USceneStateDebugControlsObject : public UObject
{
	GENERATED_BODY()

public:
	/** Debug events to push */
	UPROPERTY(EditAnywhere, Category="Event")
	TArray<FSceneStateEventTemplate> Events;
};
