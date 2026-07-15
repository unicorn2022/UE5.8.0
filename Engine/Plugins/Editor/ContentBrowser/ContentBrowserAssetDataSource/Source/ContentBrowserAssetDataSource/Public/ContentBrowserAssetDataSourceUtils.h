// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "AssetRegistry/AssetData.h"

#define UE_API CONTENTBROWSERASSETDATASOURCE_API

namespace UE::Editor::ContentBrowserAssetDataSource
{
	UE_API int32 CaptureThumbnail(const TArray<FAssetData>& InAssets);
	UE_API bool CanCaptureThumbnail(const TArray<FAssetData>& InAssets);
}

#undef UE_API
