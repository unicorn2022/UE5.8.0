// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeExternal.h"

class UEdGraphPin;
struct FSourceExternalOptions;
struct FMutableGraphGenerationContext;


UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeExternal> GenerateMutableSourceExternal(const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext, const FSourceExternalOptions& Options);

