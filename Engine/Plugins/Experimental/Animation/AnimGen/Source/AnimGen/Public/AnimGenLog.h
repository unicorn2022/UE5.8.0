// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Logging/LogMacros.h"
#include "Logging/StructuredLog.h"

#define UE_API ANIMGEN_API

UE_API DECLARE_LOG_CATEGORY_EXTERN(LogAnimGen, Log, All);

#undef UE_API