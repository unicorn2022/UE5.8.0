// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subsystems/Subsystem.h"
#include "Engine/Engine.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Subsystem)

int32 USubsystem::GetFunctionCallspace(UFunction* Function, FFrame* Stack)
{
	return GEngine->GetGlobalFunctionCallspace(Function, this, Stack);
}
