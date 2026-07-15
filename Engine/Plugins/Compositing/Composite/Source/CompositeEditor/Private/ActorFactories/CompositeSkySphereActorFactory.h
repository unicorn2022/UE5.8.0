// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "ActorFactories/ActorFactory.h"

#include "CompositeSkySphereActorFactory.generated.h"

UCLASS()
class UCompositeSkySphereActorFactory : public UActorFactory
{
	GENERATED_BODY()

public:
	/** Constructor */
	UCompositeSkySphereActorFactory(const FObjectInitializer& ObjInit);

public:
	//~ Begin UActorFactory Interface
	virtual void PostSpawnActor(UObject* Asset, AActor* NewActor) override;
	//~ End UActorFactory Interface
};
