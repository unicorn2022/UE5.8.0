// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"

#include "MetaHumanCollectionFactory.generated.h"

UCLASS()
class METAHUMANCHARACTERPALETTEEDITOR_API UMetaHumanCollectionFactory : public UFactory
{
	GENERATED_BODY()

public:

	UMetaHumanCollectionFactory();

	//~Begin UFactory interface
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetToolTip() const override;
	//~End UFactory interface
};
