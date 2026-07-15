// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MetaHumanMassGameModePerf.generated.h"

class AMassSpawner;

/**
 * Game mode that is used for MetaHuman Mass Crowd testing.
 */
UCLASS()
class AMetaHumanMassGameModePerf : public AGameModeBase
{
	GENERATED_BODY()
	
public:
	AMetaHumanMassGameModePerf();

	//~ Begin AActor interface
	virtual void BeginPlay() override;
	//~ End AActor interface

protected:
	UFUNCTION()
	void OnSpawnCompleted();

	FVector LastTargetLocation;
};
