// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"

#include "CatSoundWaveContainerFactory.generated.h"

UCLASS(MinimalAPI)
class UCatSoundWaveContainerFactory : public UFactory
{
	GENERATED_BODY()

public:
	UCatSoundWaveContainerFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};
