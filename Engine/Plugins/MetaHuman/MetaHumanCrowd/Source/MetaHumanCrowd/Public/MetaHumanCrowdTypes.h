// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshDescription.h"
#include "ReferenceSkeleton.h"
#include "Engine/SkeletalMesh.h"

#include "MetaHumanCrowdTypes.generated.h"

/**
 * A geometry bundle containing the raw data needed to construct a USkeletalMesh.
 *
 * Item pipelines produce these; the collection pipeline constructs final meshes from them.
 */
USTRUCT()
struct FMetaHumanCrowdMeshGeometryBundle
{
	GENERATED_BODY()

public:
	/** Mesh descriptions, one per LOD. Indices match the source mesh LOD indices. */
	TArray<FMeshDescription> MeshDescriptions;

	/** Reference skeleton. May differ from the source if fitting adjusted bone poses. */
	FReferenceSkeleton RefSkeleton;

	/** Materials from the source asset, used during mesh construction. */
	UPROPERTY()
	TArray<FSkeletalMaterial> Materials;

	/** Per-LOD section->material remap, mirroring USkeletalMesh::FSkeletalMeshLODInfo::LODMaterialMap. */
	TArray<TArray<int32>> LODMaterialMaps;
};


