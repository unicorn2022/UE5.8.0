// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassCommonFragments.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "MassUAFComponentFragment.generated.h"

class UUAFComponent;

USTRUCT()
struct FMassUAFComponentWrapperFragment : public FObjectWrapperFragment
{
	GENERATED_BODY()
	TWeakObjectPtr<UUAFComponent> Component;
};
