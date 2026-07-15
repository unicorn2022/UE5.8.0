// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "FastGeoFactory.generated.h"

#if WITH_EDITOR

UCLASS(hidecategories = Object, MinimalAPI)
class UFastGeoFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	// Set this in order for the factory to duplicate an object
	UPROPERTY()
	TObjectPtr<class UFastGeoTransformerSettings> InitialSettings;
	
	// Begin UFactory Interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	// End UFactory Interface	
};
#endif