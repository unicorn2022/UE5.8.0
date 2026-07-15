// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/NotNull.h"

class UMetaHumanCollection;
class UMetaHumanInstance;
class UMetaHumanWardrobeItem;
struct FAssetData;

namespace UE::MetaHuman::Analytics
{
	/** Recorded when a new MetaHuman Collection asset is created */
	void RecordCreateCollectionEvent(TNotNull<const UMetaHumanCollection*> InCollection);

	/** Recorded when a new MetaHuman Instance asset is created */
	void RecordCreateInstanceEvent(TNotNull<const UMetaHumanCollection*> InCollection, TNotNull<const UMetaHumanInstance*> InInstance);

	/** Recorded when a Collection is built via the Collection editor's Apply / Rebuild buttons */
	void RecordBuildCollectionEvent(TNotNull<const UMetaHumanCollection*> InCollection);

	/** Recorded when a Collection asset is opened for editing */
	void RecordOpenCollectionEditorEvent(TNotNull<const UMetaHumanCollection*> InCollection);

	/** Recorded when an Instance asset is opened for editing */
	void RecordOpenInstanceEditorEvent(TNotNull<const UMetaHumanCollection*> InCollection, TNotNull<const UMetaHumanInstance*> InInstance);

	/**
	 * Recorded when one or more items have been successfully added to a Collection via a drop zone.
	 *
	 * @param InCollection           The Collection that received the dropped items.
	 * @param InTargetSlotName       The slot the items were added to.
	 * @param InNumItemsAdded        The number of assets that were successfully added in this drop.
	 * @param InSampleAsset          One representative asset from the drop. For external Wardrobe
	 *                               Items, this is the principal asset the item wraps.
	 * @param InSampleWardrobeItem   The loaded Wardrobe Item if the first successful drop was an
	 *                               external Wardrobe Item asset, otherwise null.
	 */
	void RecordDropItemsOnCollectionEvent(
		TNotNull<const UMetaHumanCollection*> InCollection,
		FName InTargetSlotName,
		int32 InNumItemsAdded,
		const FAssetData& InSampleAsset,
		const UMetaHumanWardrobeItem* InSampleWardrobeItem);
}
