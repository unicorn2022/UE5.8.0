// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CONodeComponentSkeletalMesh.h"
#include "MuR/Ptr.h"

class UEdGraphPin;
struct FMutableGraphGenerationContext;

namespace UE::Mutable::Private
{
	class NodeComponent;
}


UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeComponent> GenerateMutableSourceComponent(const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext);


void FirstPass(UCONodeComponentSkeletalMesh& ComponentNode, FMutableGraphGenerationContext& GenerationContext);

