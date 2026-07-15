// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Math/VectorRegister.h"

// Platform specific vector intrinsics include.
#if !PLATFORM_ENABLE_VECTORINTRINSICS_NEON && !(defined(__cplusplus_cli)) && PLATFORM_ENABLE_VECTORINTRINSICS && UE_PLATFORM_MATH_USE_AVX
#include "Chaos/Vector32Int8AVX.h"
#else
#include "Chaos/Vector32Int8Common.h"
#endif
