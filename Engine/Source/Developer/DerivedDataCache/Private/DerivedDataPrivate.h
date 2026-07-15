// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/LowLevelMemTracker.h"
#include "Logging/LogCategory.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDerivedDataCache, Log, All);

namespace UE::DerivedData
{

LLM_DECLARE_TAG(DerivedData);
LLM_DECLARE_TAG(DerivedDataBuild);
LLM_DECLARE_TAG(DerivedDataCache);

} // UE::DerivedData
