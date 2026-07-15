// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshPaintChannelSet.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MeshPaintChannelSet)

FMeshPaintChannelDesc const* UMeshPaintChannelSet::GetChannelDesc(FName InChannelName) const
{
	for (FMeshPaintChannelDesc const& Channel : Channels)
	{
		if (InChannelName == Channel.ChannelName)
		{
			return &Channel;
		}
	}

	return nullptr;
}
