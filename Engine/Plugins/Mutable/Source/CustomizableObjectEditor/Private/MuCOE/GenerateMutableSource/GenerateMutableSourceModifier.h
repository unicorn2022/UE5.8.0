// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeModifier.h"
#include "MuR/Ptr.h"
#include "Containers/Array.h"

class UEdGraphPin;
struct FMutableGraphGenerationContext;


namespace UE::Mutable::Private
{
	TArray<Ptr<NodeModifier>> GenerateMutableSourceModifier(const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext);
}
