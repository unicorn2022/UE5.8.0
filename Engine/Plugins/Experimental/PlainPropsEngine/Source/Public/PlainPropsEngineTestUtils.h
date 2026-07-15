// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

namespace PlainProps::UE
{
enum class EBindMode : uint8;
enum class EBatchType : uint8;

PLAINPROPSENGINE_API void BindAllTypes(EBindMode Mode, EBatchType BatchType);

}