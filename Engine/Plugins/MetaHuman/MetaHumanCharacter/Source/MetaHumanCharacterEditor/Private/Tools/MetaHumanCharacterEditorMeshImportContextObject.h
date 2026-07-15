// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCharacterTargetKeyPoints.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"

#include "MetaHumanCharacterEditorMeshImportContextObject.generated.h"

/**
 * Context object stored in the InteractiveToolsContext's ContextObjectStore by
 * UMetaHumanCharacterEditorMode.  It caches the two dynamic meshes that were last
 * built for a given target-mesh key so that UMetaHumanCharacterEditorMeshImportTool
 * can re-use them without triggering another async asset load + conversion when the
 * tool is restarted against the same target mesh.
 */
UCLASS(Transient)
class UMetaHumanCharacterEditorMeshImportContextObject : public UObject
{
	GENERATED_BODY()

public:
	/** Key that identifies which target mesh the cached dynamic meshes were built from. */
	FMetaHumanCharacterTargetMeshKey TargetMeshKey;

	/**
	 * Cached body (or combined) dynamic mesh.  Valid when the key is non-empty and
	 * the last load succeeded.
	 */
	TSharedPtr<UE::Geometry::FDynamicMesh3> BodyDynamicMesh;

	/**
	 * Cached head dynamic mesh.  Only populated when the tool was in "mesh parts" mode
	 * (bUseCharacterParts == true) and a head mesh was provided.
	 */
	TSharedPtr<UE::Geometry::FDynamicMesh3> HeadDynamicMesh;

	/**
	 * Cached AABB tree for the body (or combined) dynamic mesh.  Kept in sync with
	 * BodyDynamicMesh and valid whenever BodyDynamicMesh is valid.
	 */
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> BodyAABBTree;

	/**
	 * Cached AABB tree for the head dynamic mesh.  Only populated alongside HeadDynamicMesh
	 * in "mesh parts" mode.
	 */
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> HeadAABBTree;

	/** Returns true when the store holds meshes that match @p InKey. */
	bool HasValidMeshesForKey(const FMetaHumanCharacterTargetMeshKey& InKey) const
	{
		return BodyDynamicMesh.IsValid() && TargetMeshKey == InKey;
	}

	/** Stores new dynamic meshes and their AABB trees for the given key, replacing any previously cached data. */
	void SetCachedMeshes(
		const FMetaHumanCharacterTargetMeshKey& InKey,
		TSharedPtr<UE::Geometry::FDynamicMesh3> InBodyDynamicMesh,
		TSharedPtr<UE::Geometry::FDynamicMesh3> InHeadDynamicMesh,
		TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> InBodyAABBTree,
		TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> InHeadAABBTree)
	{
		TargetMeshKey   = InKey;
		BodyDynamicMesh = MoveTemp(InBodyDynamicMesh);
		HeadDynamicMesh = MoveTemp(InHeadDynamicMesh);
		BodyAABBTree    = MoveTemp(InBodyAABBTree);
		HeadAABBTree    = MoveTemp(InHeadAABBTree);
	}

	/** Clears any cached mesh data. */
	void Invalidate()
	{
		TargetMeshKey = FMetaHumanCharacterTargetMeshKey{};
		BodyDynamicMesh.Reset();
		HeadDynamicMesh.Reset();
		BodyAABBTree.Reset();
		HeadAABBTree.Reset();
	}
};
