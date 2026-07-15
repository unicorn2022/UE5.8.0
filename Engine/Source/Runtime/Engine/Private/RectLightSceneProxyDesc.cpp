// Copyright Epic Games, Inc. All Rights Reserved.

#include "RectLightSceneProxyDesc.h"

void FRectLightSceneProxyDesc::Serialize(FArchive& Ar)
{
	FLocalLightSceneProxyDesc::Serialize(Ar);

	Ar << SourceWidth;
	Ar << SourceHeight;
	Ar << BarnDoorAngle;
	Ar << BarnDoorLength;
	Ar << LightFunctionConeAngle;
	Ar << SourceTexture;
	Ar << SourceTextureScale;
	Ar << SourceTextureOffset;
}
