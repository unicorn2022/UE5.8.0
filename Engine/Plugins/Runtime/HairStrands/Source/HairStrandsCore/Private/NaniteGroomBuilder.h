// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once


#include "CoreMinimal.h"
#include "NaniteGroomAsset.h"

#if WITH_EDITOR

#include "GroomAsset.h"
#include "Rendering/NaniteResources.h"

#define UE_API HAIRSTRANDSCORE_API

class FGroomAssetNaniteBuilder
{
public:
	FGroomAssetNaniteBuilder() = default;
	UE_API bool Build(TPimplPtr<Nanite::FResources>& OutNaniteResourcesPtr, const UGroomAsset* InGroomAsset);
	~FGroomAssetNaniteBuilder() = default;
};

#undef UE_API

#endif // WITH_EDITOR