// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MovementInterface.generated.h"

#define UE_API ENGINE_API

/**
 * An interface for movement concepts that can be used to gather basic movement related information from objects across different movement systems.
 * This interface is meant to be very broad and simple with trying make as little assumptions as possible so we don't limit movement system compatibility
 */
UINTERFACE(MinimalAPI, NotBlueprintable)
class UMovementInterface : public UInterface
{
	GENERATED_BODY()
};

class IMovementInterface
{
	GENERATED_BODY()

public:
	/** 
	 * Returns the object that owns the movement system. In a Actor/ActorComponent set up, this will typically be an Actor
	 * Note: This can return nullptr with certain movement setups
	 */
	UFUNCTION(BlueprintCallable, Category = "Movement")
	virtual UObject* GetMovementOwner() const = 0;
	
	/** 
	 * Returns the root object being moved (updated) by the movement system 
	 * Note: This can return nullptr with certain movement setups
	 */
	UFUNCTION(BlueprintCallable, Category = "Movement")
	virtual UObject* GetUpdatedObject() const = 0;
	
	/** Returns velocity of object moved by the movement system */
	UFUNCTION(BlueprintCallable, Category = "Movement")
	virtual FVector GetVelocity() const = 0;
	
	/** Returns world space Transform of object moved by the movement system */
	UFUNCTION(BlueprintCallable, Category = "Movement")
	virtual FTransform GetTransform() const = 0;
	
	/** Stops active movement. This function does NOT cancel future movement.
	 * Note: depending on the movement system this may take effect immediately or next movement update
	 */
	UFUNCTION(BlueprintCallable, Category = "Movement")
	virtual void RequestStopMovement() = 0;
};

#undef UE_API