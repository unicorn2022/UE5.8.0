// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlainPropsTypes.h"
#include "PlainPropsSave.h"

struct FOverriddenPropertySet;
struct FOverriddenPropertyNode;

namespace PlainProps
{

namespace UE
{

FName GetOverridePropertyName(const FOverriddenPropertyNode& Override);

FBuiltStruct* SaveStructOverrides(const FOverriddenPropertySet& Overrides, FBaseline Base, FBindId BindId, const FSaveContext& Context);

const FOverriddenPropertySet* GetSaveOverrides();

} // namespace UE
} // namespace PlainProps
