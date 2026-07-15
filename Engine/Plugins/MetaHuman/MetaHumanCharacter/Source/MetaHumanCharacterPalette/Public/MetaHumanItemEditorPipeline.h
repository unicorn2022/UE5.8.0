// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCharacterEditorPipeline.h"

#include "MetaHumanCharacterPalette.h"

#include "Misc/NotNull.h"
#include "Tasks/Task.h"

#include "MetaHumanItemEditorPipeline.generated.h"

struct FInstancedStruct;
struct FMetaHumanPaletteBuildCacheEntry;
struct FMetaHumanPaletteBuiltData;
struct FMetaHumanPinnedSlotSelection;
class UMetaHumanItemPipeline;

#define UE_API METAHUMANCHARACTERPALETTE_API

/**
 * The editor-only component of a UMetaHumanItemPipeline.
 */
UCLASS(Abstract, MinimalAPI)
class UMetaHumanItemEditorPipeline : public UMetaHumanCharacterEditorPipeline
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	DECLARE_DELEGATE_OneParam(FOnBuildComplete, FMetaHumanPaletteBuiltData&& /* BuiltData */);

	/**
	 * Builds the item based on input from the hosting Collection pipeline. 
	 * 
	 * For example, the build input could be a body mesh that this item pipeline should fit 
	 * clothing to.
	 * 
	 * Can only be called from a Collection or Item pipeline. Items can't be built by themselves,
	 * they need to be hosted by a Collection.
	 * 
	 * SortedPinnedSlotSelections will be filtered to only include this item and its sub-items. If
	 * this item is pinned, it will be in SortedPinnedSlotSelections.
	 * 
	 * SortedItemsToExclude will be filtered to only include sub-items of this item. If this item
	 * is excluded, BuildItem won't be called at all, so there is no need to search 
	 * SortedItemsToExclude for this item.
	 */
	struct FBuildItemParams
	{
		FMetaHumanPaletteItemPath ItemPath;
		TNotNull<const UMetaHumanWardrobeItem*> WardrobeItem;
		EMetaHumanCharacterPaletteBuildQuality Quality = EMetaHumanCharacterPaletteBuildQuality::Production;
		TNotNull<UObject*> OuterForGeneratedObjects;
		FInstancedStruct BuildInput;
		TConstArrayView<FMetaHumanPinnedSlotSelection> SortedPinnedSlotSelections;
		TConstArrayView<FMetaHumanPaletteItemPath> SortedItemsToExclude;
		FGuid BuildCacheGuid;
	};

	virtual UE::Tasks::TTask<FMetaHumanPaletteBuiltData> BuildItem(const FBuildItemParams& Params) const
		PURE_VIRTUAL(UMetaHumanItemEditorPipeline::BuildItem, return {};);

	UE_DEPRECATED(5.8, "TargetPlatform is no longer used at build time. Use the new BuildItem overload.")
	UE_API void BuildItem(
		const FMetaHumanPaletteItemPath& ItemPath,
		TNotNull<const UMetaHumanWardrobeItem*> WardrobeItem,
		const FInstancedStruct& BuildInput,
		TConstArrayView<FMetaHumanPinnedSlotSelection> SortedPinnedSlotSelections,
		TConstArrayView<FMetaHumanPaletteItemPath> SortedItemsToExclude,
		FMetaHumanPaletteBuildCacheEntry& BuildCache,
		EMetaHumanCharacterPaletteBuildQuality Quality,
		ITargetPlatform* TargetPlatform,
		TNotNull<UObject*> OuterForGeneratedObjects,
		const FOnBuildComplete& OnComplete) const;

	UE_API virtual void BuildItemSynchronous(const FBuildItemParams& Params, FMetaHumanPaletteBuiltData& OutBuiltData) const;

	UE_DEPRECATED(5.8, "TargetPlatform is no longer used at build time. Use the new BuildItemSynchronous overload.")
	UE_API void BuildItemSynchronous(
		const FMetaHumanPaletteItemPath& ItemPath,
		TNotNull<const UMetaHumanWardrobeItem*> WardrobeItem,
		const FInstancedStruct& BuildInput,
		TConstArrayView<FMetaHumanPinnedSlotSelection> SortedPinnedSlotSelections,
		TConstArrayView<FMetaHumanPaletteItemPath> SortedItemsToExclude,
		FMetaHumanPaletteBuildCacheEntry& BuildCache,
		EMetaHumanCharacterPaletteBuildQuality Quality,
		ITargetPlatform* TargetPlatform,
		TNotNull<UObject*> OuterForGeneratedObjects,
		FMetaHumanPaletteBuiltData& OutBuiltData) const;

	/** 
	 * Unpacks the assets contained in the built data for this item and any sub-items.
	 * 
	 * Calls the provided TryUnpackObjectDelegate to do the actual unpacking.
	 * 
	 * This base implementation should be good enough for most pipelines, but there is the option
	 * to override it if any assets need special handling.
	 */
	UE_API virtual bool TryUnpackItemAssets(
		TNotNull<const UMetaHumanWardrobeItem*> WardrobeItem,
		const FMetaHumanPaletteItemPath& BaseItemPath,
		FMetaHumanPipelineBuiltDataCollectionMutableView ItemBuiltData,
		const FString& UnpackFolder,
		const FTryUnpackObjectDelegate& TryUnpackObjectDelegate) const;

	/** Returns the runtime pipeline instance corresponding to this editor pipeline instance. */
	UE_API virtual TNotNull<const UMetaHumanItemPipeline*> GetRuntimePipeline() const;

	/** Calls GetRuntimePipeline. No need for subclasses to implement this. */
	UE_API virtual TNotNull<const UMetaHumanCharacterPipeline*> GetRuntimeCharacterPipeline() const override;
#endif // WITH_EDITOR
};

#undef UE_API
