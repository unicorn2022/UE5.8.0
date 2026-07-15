// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuT/NodeSkeletalMesh.h"

struct FSourceSkeletalMeshOptions;
struct FMutableGraphGenerationContext;
class UEdGraphPin;

namespace UE::Mutable::Private
{
	Ptr<NodeSkeletalMesh> GenerateMutableSourceSkeletalMesh(const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext, const FSourceSkeletalMeshOptions& Options);	
}
