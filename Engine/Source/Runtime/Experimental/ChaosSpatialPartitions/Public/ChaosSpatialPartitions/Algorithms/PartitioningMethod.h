// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

UE_EXPERIMENTAL(5.8, "The new spatial partition module is experimental")
namespace Chaos::SpatialPartition
{
	// Methods for partitioning objects when building a spatial partition.
	enum EPartitioningMethod
	{
		// Split using the spatial median (aabb center) of the object centers.
		CentroidSpatialMedian,
		// Split using the variance of the object centers.
		CentroidVariance,
		// Split using a surface area heuristic.
		SurfaceArea,
	};
} // namespace Chaos::SpatialPartition
