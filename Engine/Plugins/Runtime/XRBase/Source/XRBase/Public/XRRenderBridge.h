// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHIResources.h"

class  FXRRenderBridge : public FRHICustomPresent
{
public:

	FXRRenderBridge() {}

	// virtual methods from FRHICustomPresent

	virtual void OnBackBufferResize() override {}

	virtual bool NeedsNativePresent() override
	{
		return true;
	}
};
