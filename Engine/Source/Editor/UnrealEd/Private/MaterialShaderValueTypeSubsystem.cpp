// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subsystems/MaterialShaderValueTypeSubsystem.h"
#include "MaterialGraph/MaterialShaderValueTypeObject.h"

UMaterialShaderValueTypeObject* UMaterialShaderValueTypeSubsystem::GetOrCreate(UE::Shader::EValueType InType)
{
	TObjectPtr<UMaterialShaderValueTypeObject>& Slot = Flyweights.FindOrAdd(static_cast<uint8>(InType));
	if (!Slot)
	{
		Slot = NewObject<UMaterialShaderValueTypeObject>(this);
		Slot->ValueType = InType;
	}
	return Slot;
}
