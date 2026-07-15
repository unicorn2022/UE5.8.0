// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"

#include "ModularVehicleSkeletalPawn.generated.h"

class USkeletalMeshComponent;
class UModularVehicleBaseComponent;

UCLASS(MinimalAPI)
class AModularVehicleSkeletalPawn: public APawn
{
	GENERATED_UCLASS_BODY()

public:
	UFUNCTION()
	USkeletalMeshComponent* GetSkeletalMeshComponent() const { return SkeletalMeshComponent; }

	UFUNCTION()
	UModularVehicleBaseComponent* GetVehicleSimulationComponent() const { return VehicleSimComponent; }

	/* SkeletalMeshComponent root component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Vehicle, meta = (ExposeFunctionCategories = "Components|ModularVehicle", AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent;

	/* ModularVehicleComponent */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Vehicle, meta = (ExposeFunctionCategories = "Components|ModularVehicle", AllowPrivateAccess = "true"))
	TObjectPtr<UModularVehicleBaseComponent> VehicleSimComponent;

};
