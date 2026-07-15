// Copyright Epic Games, Inc. All Rights Reserved.

#include "TmvStructuredBufferSwizzlingShader.h"
#include "GlobalShader.h"
#include "ShaderCore.h"
#include "ShaderParameterMacros.h"
#include "CoreMinimal.h"

IMPLEMENT_GLOBAL_SHADER(FTmvStructuredBufferSwizzlePS, "/Plugin/TmvMedia/Private/TmvStructuredBufferSwizzler.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FTmvSwizzleVS, "/Plugin/TmvMedia/Private/TmvStructuredBufferSwizzler.usf", "MainVS", SF_Vertex);
