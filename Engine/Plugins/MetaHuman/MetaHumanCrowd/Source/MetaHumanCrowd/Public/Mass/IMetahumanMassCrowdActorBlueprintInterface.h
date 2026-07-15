// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "Animation/TrajectoryTypes.h"
#include "MetaHumanMassAnimDesc.h"

#include "IMetahumanMassCrowdActorBlueprintInterface.generated.h" 

UINTERFACE(BlueprintType, MinimalAPI)
class UMetahumanMassCrowdActorBlueprintInterface : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

class UAnimSequence;

/**
 * Blueprint interface for Actor classes used in the MetaHuman Mass crowd system on the Actor representation path.
 * Implement this interface on any Actor Blueprint that should be driven by Mass.
 * Each tick Mass read/writes animation state into the Actor for ISKM synchronization and locomotion. 
 */
class IMetahumanMassCrowdActorBlueprintInterface : public IInterface
{
	GENERATED_IINTERFACE_BODY()

public:	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Animation")
	void SetMetaHumanMassAnimDesc(const FMetahumanMassAnimDesc& NewAnimDesc);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Animation")
	FMetahumanMassAnimDesc GetMetaHumanMassAnimDesc() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Animation")
	void SetTrajectory(const FTransformTrajectory& NewTrajectory);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Animation")
	void SetAnimations(const TMap<FName, UAnimSequence*>& Animations);
};

