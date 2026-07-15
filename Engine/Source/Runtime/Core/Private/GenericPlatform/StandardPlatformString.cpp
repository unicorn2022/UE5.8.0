// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenericPlatform/StandardPlatformString.h"

// only IOS and Mac were using this file before, but that has all moved over to the FullReplacementPlatformString
#if !PLATFORM_USE_SYSTEM_VSWPRINTF && !PLATFORM_TCHAR_IS_CHAR16

#include "GenericPlatform/StandardPlatformString.h"
#include "HAL/UnrealMemory.h"
#include "Templates/UnrealTemplate.h"
#include "Logging/LogCategory.h"
#include "Logging/LogMacros.h"

#endif // !PLATFORM_USE_SYSTEM_VSWPRINTF