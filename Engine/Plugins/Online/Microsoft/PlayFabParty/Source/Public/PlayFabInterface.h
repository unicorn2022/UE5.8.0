// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Do we have a cached Entity ID for the provided Xuid? */
PLAYFABPARTY_API bool HaveEntityIdForXuid(const uint64 Xuid);
/** Get the cached Entity ID for the provided Xuid. Asserts if we do not have a value cached! */
PLAYFABPARTY_API const char* GetEntityIdForXuid(const uint64 Xuid);
/** Get the cached Entity Token for the provided Xuid if any. */
PLAYFABPARTY_API const char* GetEntityTokenForXuid(const uint64 Xuid);
