// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace VEUV
{
	struct FMesh
	{
		/** All vertices, assumed all used */
		TArray<FVector3f> Vertices;
		
		/** All triangles */
		TArray<FInt32Vector3> Faces;
	};
}
