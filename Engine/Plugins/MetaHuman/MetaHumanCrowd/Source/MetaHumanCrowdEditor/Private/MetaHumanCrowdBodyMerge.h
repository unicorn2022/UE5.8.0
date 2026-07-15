// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanGeometryRemovalTypes.h"
#include "MetaHumanPaletteItemKey.h"

class USkeleton;
struct FMetaHumanCrowdMeshGeometryBundle;

namespace UE::MetaHuman::CrowdBodyMerge
{
	static constexpr uint8 UnclaimedSlotIndex = MAX_uint8;

	/** Data for a single outfit item within a slot, including its per-body geometry bundles and face map. */
	struct FSlotItemData
	{
		/** Map from body item key to the fitted outfit geometry bundle for this item.
		  * Bundles are modified in place -- body geometry is appended to them. */
		TMap<FMetaHumanPaletteItemKey, FMetaHumanCrowdMeshGeometryBundle*> BodyToOutfitGeometryMap;

		/** The body hidden face map image for this outfit item (CPU-side). */
		GeometryRemoval::FHiddenFaceMapImage BodyHiddenFaceMapImage;

		/** Whether this item has a valid body hidden face map. */
		bool bHasValidFaceMap = false;
	};

	/** Data for a single clothing slot (virtual slot), which may contain multiple outfit items. */
	struct FSlotData
	{
		/** The virtual slot name (e.g. "Top Garment"). */
		FName VirtualSlotName;

		/** All outfit items that belong to this slot. */
		TArray<FSlotItemData> Items;

		/** Max bone weight observed per bone name for vertices owned by this slot. */
		TMap<FName, float> MaxBoneWeights;

		/** Color used for debug visualization of this slot. */
		FLinearColor SlotColor = FLinearColor::White;
	};

	/**
	 * Merges body geometry onto fitted outfit geometry bundles so that visible parts of the body
	 * (arms, hands, etc.) are rendered as part of the clothing mesh instead of a separate body mesh.
	 *
	 * @param BodyGeometryBundles  Map from body item key to the body geometry bundle.
	 * @param Slots                Array of slot data (one per virtual slot with face maps). Modified in place.
	 * @param LODIndicesToProcess  The union of actor and instanced body LOD indices to process.
	 * @param TargetSkeleton       The shared skeleton all meshes will be rebound to.
	 * @param DebugImagePath       If non-empty and the debug cvar is enabled, writes a debug PNG of the
	 *                             ownership bitmap to this path.
	 */
	void MergeBodyOntoOutfits(
		const TMap<FMetaHumanPaletteItemKey, FMetaHumanCrowdMeshGeometryBundle*>& BodyGeometryBundles,
		TArray<FSlotData>& Slots,
		TConstArrayView<int32> LODIndicesToProcess,
		const USkeleton* TargetSkeleton,
		const FString& DebugImagePath = FString());

} // namespace UE::MetaHuman::CrowdBodyMerge
