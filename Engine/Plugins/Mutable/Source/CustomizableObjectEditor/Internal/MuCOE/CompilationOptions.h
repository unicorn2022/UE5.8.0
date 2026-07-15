// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCO/CustomizableObjectCompilerTypes.h"

class UCustomizableObject;

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API


UE_API FCompilationOptions GetCompilationOptions(const UCustomizableObject& Object);


#undef UE_API