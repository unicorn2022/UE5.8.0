// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialBridge/Slot/AvaMaterialBridgeSlot.h"

namespace UE::Ava
{

FMaterialBridgeSlot::FMaterialBridgeSlot(UMaterialInterface* InMaterial, const FSlotId& InSlotId)
	: Material(InMaterial)
	, SlotId(InSlotId)
{
}

} // UE::Ava
