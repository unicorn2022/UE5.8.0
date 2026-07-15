// Copyright Epic Games, Inc. All Rights Reserved.

#include "SpotLightSceneProxyDesc.h"

void FSpotLightSceneProxyDesc::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar << InnerConeAngle;
	Ar << OuterConeAngle;
}