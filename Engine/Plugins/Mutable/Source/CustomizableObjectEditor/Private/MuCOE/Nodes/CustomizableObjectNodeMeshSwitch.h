// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CONodeSwitch.h"

#include "CustomizableObjectNodeMeshSwitch.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API


// DEPRECATED
UCLASS(NotPlaceable, HideDropdown)
class UCustomizableObjectNodeMeshSwitch : public UCONodeSwitch
{
public:
	GENERATED_BODY()

	// UCustomizableObjectNodeSwitch interface
	virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;
};

#undef UE_API
