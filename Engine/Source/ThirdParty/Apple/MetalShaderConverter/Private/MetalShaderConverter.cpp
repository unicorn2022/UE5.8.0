// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalShaderConverter.cpp: Metal device RHI implementation.
=============================================================================*/

#include "HAL/Platform.h"

#if PLATFORM_MAC || PLATFORM_IOS || PLATFORM_TVOS || PLATFORM_VISIONOS

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
#include "metal_irconverter.h"
#define IR_RUNTIME_METALCPP 1
#define IR_PRIVATE_IMPLEMENTATION 1
#include "metal_irconverter_runtime.h"
#endif

#endif
