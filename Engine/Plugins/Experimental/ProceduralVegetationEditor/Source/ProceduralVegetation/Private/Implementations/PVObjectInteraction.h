// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Nodes/PVObjectInteractionSettings.h"

struct FMeshDescriptionTriangleMeshAdapter;

struct FPVObjectInteraction
{
	static FVector GetTriangleNormal(
		FMeshDescriptionTriangleMeshAdapter& Mesh,
		const int32 TID,
		TOptional<FVector> BaryCoords = TOptional<FVector>()
	);
	
	static void ObjectInteraction(FManagedArrayCollection& OutCollection, FPVColliderParams Collider);
};
