// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"

#if !UE_BUILD_SHIPPING

extern APPFRAMEWORK_API const FLazyName StarshipGalleryTabName;
APPFRAMEWORK_API void RegisterStarshipGalleryTabSpawner();
APPFRAMEWORK_API void UnregisterStarshipGalleryTabSpawner();
APPFRAMEWORK_API void RestoreStarshipSuite();

#endif // #if !UE_BUILD_SHIPPING
