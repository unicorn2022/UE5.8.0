// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuT/NodeSkeletalMesh.h"
#include "MuT/NodeSkeletalMeshMerge.h"
#include "MuT/NodeSkeletalMeshNew.h"
#include "MuT/NodeSkeletalMeshObjectParameter.h"

struct FSourceSkeletalMeshObjectOptions;
class UCustomizableObjectNodeSkeletalMeshParameter;
struct FMutableGraphGenerationContext;
class UEdGraphPin;

namespace UE::Mutable::Private
{
	Ptr<NodeSkeletalMeshObject> GenerateMutableSourceSkeletalMeshObject(const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext, const FSourceSkeletalMeshObjectOptions& Options);
	
	Ptr<NodeSkeletalMeshObjectParameter> GenerateMutableSourceSkeletalMeshObjectParameter(const UCustomizableObjectNodeSkeletalMeshParameter& Node, FMutableGraphGenerationContext& GenerationContext);
}
