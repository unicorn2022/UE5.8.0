// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialBridge/Slot/AvaMaterialBridgeWriteSlot.h"

namespace UE::Ava
{

void FMaterialBridgeWriteSlot::SetMaterial(UMaterialInterface* InMaterial)
{
	if (InMaterial != Material)
	{
		SetMaterialInternal(InMaterial);
		OnMaterialChanged();
	}
}

bool FMaterialBridgeWriteSlot::RequestFeature(TConstStructView<FAvaMaterialBridgeFeature> InFeature)
{
	if (InFeature.IsValid())
	{
		return OnFeatureRequest(InFeature);
	}
	return false;
}

void FMaterialBridgeWriteSlot::SetMaterialInternal(UMaterialInterface* InMaterial)
{
	Material = InMaterial;
}

} // UE::Ava
