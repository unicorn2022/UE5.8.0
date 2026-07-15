// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ThumbnailRendering/SkeletalMeshThumbnailRenderer.h"

#include "CustomizableObjectSkeletalMeshThumbnailRenderer.generated.h"

UCLASS()
class UCustomizableObjectSkeletalMeshThumbnailRenderer : public USkeletalMeshThumbnailRenderer
{
	GENERATED_BODY()
public:

	void EvictThumbnailScene(const UObject* Object)
	{
		ThumbnailSceneCache.RemoveThumbnailScene(Object);
	}
};
