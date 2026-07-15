// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AvaMaterialBridgeSlot.h"

#define UE_API AVALANCHEMATERIAL_API


namespace UE::Ava
{

/** Short-lived representation of an accessed material slot for read */
class FMaterialBridgeReadSlot : public FMaterialBridgeSlot
{
public:
	using FMaterialBridgeSlot::FMaterialBridgeSlot;
};

} // UE::Ava

#undef UE_API
