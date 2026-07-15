// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "EditableComputeGraphFactory.generated.h"

UCLASS(hidecategories = Object)
class UEditableComputeGraphFactory : public UFactory
{
	GENERATED_BODY()

	UEditableComputeGraphFactory();

	//~ Begin UFactory Interface.
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	//~ End UFactory Interface.
};
