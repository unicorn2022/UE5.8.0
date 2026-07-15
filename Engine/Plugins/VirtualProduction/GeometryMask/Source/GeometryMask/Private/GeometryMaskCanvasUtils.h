// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/NotNull.h"

class UTextureRenderTarget2DArray;

namespace UE::GeometryMask
{
	/** Updates the resource for the render target */
	void UpdateRenderTarget(TNotNull<UTextureRenderTarget2DArray*> InRenderTarget);

} // UE::GeometryMask
