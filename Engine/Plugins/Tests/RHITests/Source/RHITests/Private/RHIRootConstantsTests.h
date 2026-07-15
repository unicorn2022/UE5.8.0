// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class FRHICommandListImmediate;

namespace RHIRootConstantsTests
{
	bool Test_GraphicsShaderRootConstants(FRHICommandListImmediate& RHICmdList);
	bool Test_ComputeShaderRootConstants(FRHICommandListImmediate& RHICmdList);
}
