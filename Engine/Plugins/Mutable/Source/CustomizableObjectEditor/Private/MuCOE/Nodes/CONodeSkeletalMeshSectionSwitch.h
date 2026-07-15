// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CONodeSwitch.h"

#include "CONodeSkeletalMeshSectionSwitch.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API


// DEPRECATED
UCLASS(NotPlaceable, HideDropdown)
class UCONodeSkeletalMeshSectionSwitch : public UCONodeSwitch
{
public:
	GENERATED_BODY()

	// UCustomizableObjectNode interface
	virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;
};


#undef UE_API
