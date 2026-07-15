// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/VirtualValueBundle_ValueBundle.h"

namespace UE::UAF
{

const FAnimNextGraphLODPose* FVirtualValueBundle_ValueBundle::GetLODPose() const
{
	return &Pose;
}

const FValueBundleHeap* FVirtualValueBundle_ValueBundle::GetValueBundle() const
{
	return &ValueBundle;
}

}
