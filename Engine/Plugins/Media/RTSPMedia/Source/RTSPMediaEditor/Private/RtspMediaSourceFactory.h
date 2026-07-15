// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"

#include "RtspMediaSourceFactory.generated.h"

UCLASS()
class URtspMediaSourceFactory : public UFactory
{
	GENERATED_BODY()

public:
	URtspMediaSourceFactory(const FObjectInitializer& ObjectInitializer);

	//~ Begin UFactory
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* InContext, FFeedbackContext* InWarn) override;
	virtual uint32 GetMenuCategories() const override;
	virtual bool ShouldShowInNewMenu() const override;
	//~ End UFactory
};