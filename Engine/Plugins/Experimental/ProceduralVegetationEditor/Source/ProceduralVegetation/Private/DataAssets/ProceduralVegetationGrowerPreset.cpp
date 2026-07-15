// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataAssets/ProceduralVegetationGrowerPreset.h"

void UProceduralVegetationGrowerPreset::PostLoad()
{
	Super::PostLoad();

	if (GrowthParams.Auxin.MinGravitationalDot != 0.0f)
	{
		GrowthParams.GravityParams.MinGravitationalDot = GrowthParams.Auxin.MinGravitationalDot;
		GrowthParams.Auxin.MinGravitationalDot = 0.0f;
	}
}
