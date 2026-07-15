// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassSkinnedMeshRepresentationTypes.h"
#include "Components/InstancedSkinnedMeshComponent.h"

//-----------------------------------------------------------------------------
// FMassSkinnedMeshInstanceVisualizationMeshDesc
//-----------------------------------------------------------------------------
FMassSkinnedMeshInstanceVisualizationMeshDesc::FMassSkinnedMeshInstanceVisualizationMeshDesc()
{
	InstancedSkinnedMeshComponentClass = UInstancedSkinnedMeshComponent::StaticClass();
}

//-----------------------------------------------------------------------------
// FSkinnedMeshInstanceVisualizationDesc
//-----------------------------------------------------------------------------
bool FSkinnedMeshInstanceVisualizationDesc::IsValid() const
{
	for (const FMassSkinnedMeshInstanceVisualizationMeshDesc& MeshDesc : Meshes)
	{
		if (MeshDesc.Asset && MeshDesc.InstancedSkinnedMeshComponentClass)
		{
			return true;
		}
	}
	return false;
}

void FMassInstancedSkinnedMeshInfo::Reset()
{
	Desc.Reset();
	InstancedSkinnedMeshComponents.Reset();
	LODSignificanceRanges.Reset();
}
