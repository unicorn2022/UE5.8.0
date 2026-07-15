// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CONodeSkeletalMeshVariation.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"


FName UCONodeSkeletalMeshVariation::GetCategory() const
{
	return UEdGraphSchema_CustomizableObject::PC_SkeletalMesh;
}

