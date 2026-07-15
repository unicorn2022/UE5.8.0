// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHITestsCommon.h"

class FRHIShaderBundleTests
{
public:
	static bool Test_ComputeShaderBundle(FRHICommandListImmediate& RHICmdList);
	static bool Test_GraphicsShaderBundle_MSPS(FRHICommandListImmediate& RHICmdList);
	static bool Test_GraphicsShaderBundle_VSPS(FRHICommandListImmediate& RHICmdList);
};
