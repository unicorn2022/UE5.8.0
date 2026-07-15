// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CustomizableSkeletalComponentPrivate.generated.h"

#define UE_API CUSTOMIZABLEOBJECT_API

class UCustomizableObjectInstanceUsage;

UCLASS(MinimalAPI)
class UCustomizableSkeletalComponentPrivate : public UObject
{
	GENERATED_BODY()

public:
	UE_API UCustomizableSkeletalComponentPrivate();
	
	UPROPERTY(Instanced)
	TObjectPtr<UCustomizableObjectInstanceUsage> InstanceUsage;
};

#undef UE_API
