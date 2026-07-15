// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "StereoRenderTargetManager.h"

#define UE_API XRBASE_API

class FRHIViewport;

/** 
 * Common IStereoRenderTargetManager implementation that can be used by HMD implementations in order to get default implementations for most methods.
 */
class FXRRenderTargetManager : public IStereoRenderTargetManager
{
public:
	/**
	 * Calculates dimensions of the render target texture for direct rendering of distortion.
	 * This implementation calculates the size based on the current value of xr.SecondaryScreenPercentage.HMDRenderTarget.
	 */
	UE_API virtual void CalculateRenderTargetSize(const class FViewport& Viewport, uint32& InOutSizeX, uint32& InOutSizeY) override;

	/**
	* Returns true, if render target texture must be re-calculated.
	*/
	UE_API virtual bool NeedReAllocateViewportRenderTarget(const class FViewport& Viewport) override;
};

#undef UE_API
