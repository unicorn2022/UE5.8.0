// Copyright Epic Games, Inc. All Rights Reserved.

#include "LocalLightSceneProxyDesc.h"

void FLocalLightSceneProxyDesc::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar << MaxDrawDistance;
	Ar << MaxDistanceFadeRange;
	Ar << InverseExposureBlend;
	Ar << AttenuationRadius;
}