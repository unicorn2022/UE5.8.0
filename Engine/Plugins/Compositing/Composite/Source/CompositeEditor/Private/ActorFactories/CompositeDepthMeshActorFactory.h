// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "ActorFactories/ActorFactory.h"

#include "CompositeDepthMeshActorFactory.generated.h"

UCLASS()
class UCompositeDepthMeshActorFactory : public UActorFactory
{
	GENERATED_BODY()

public:
	/** Constructor */
	UCompositeDepthMeshActorFactory(const FObjectInitializer& ObjInit);

public:
	//~ Begin UActorFactory interface
	virtual void PostSpawnActor(UObject* Asset, AActor* NewActor) override;
	//~ End UActorFactory interface
};
