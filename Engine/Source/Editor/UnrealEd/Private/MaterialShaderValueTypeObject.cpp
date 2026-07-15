// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialGraph/MaterialShaderValueTypeObject.h"
#include "Subsystems/MaterialShaderValueTypeSubsystem.h"
#include "Editor.h"

UMaterialShaderValueTypeObject* UMaterialShaderValueTypeObject::Get(UE::Shader::EValueType InType)
{
	check(IsInGameThread());

	if (InType == UE::Shader::EValueType::Void)
	{
		return nullptr;
	}

	if (!GEditor)
	{
		return nullptr;
	}

	UMaterialShaderValueTypeSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMaterialShaderValueTypeSubsystem>();
	if (!Subsystem)
	{
		return nullptr;
	}

	return Subsystem->GetOrCreate(InType);
}
