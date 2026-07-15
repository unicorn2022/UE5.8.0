// Copyright Epic Games, Inc. All Rights Reserved.

#include "TmvTextureBufferSwizzlingShader.h"
#include "GlobalShader.h"
#include "ShaderCore.h"
#include "ShaderParameterMacros.h"
#include "CoreMinimal.h"

IMPLEMENT_GLOBAL_SHADER(FTmvTextureBufferSwizzlePS, "/Plugin/TmvMedia/Private/TmvTextureBufferSwizzler.usf", "MainPS", SF_Pixel);
