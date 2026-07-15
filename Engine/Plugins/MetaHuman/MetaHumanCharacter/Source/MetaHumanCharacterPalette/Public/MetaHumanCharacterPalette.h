// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#include "MetaHumanCharacterPaletteItem.h"
#include "MetaHumanCharacterPipeline.h"
#include "MetaHumanPipelineBuiltDataCollection.h"

#include "HAL/CriticalSection.h"
#include "Interfaces/ITargetPlatform.h"
#include "Misc/NotNull.h"
#include "StructUtils/InstancedStruct.h"

#include "MetaHumanCharacterPalette.generated.h"

class UMetaHumanCharacterPipeline;

/** 
 * The output of the build for an entire palette, include its items and items' items, etc.
 */
USTRUCT()
struct FMetaHumanPaletteBuiltData
{
	GENERATED_BODY()

public:
	/** Returns true if there is any build output for this specific item */
	METAHUMANCHARACTERPALETTE_API bool HasBuildOutputForItem(const FMetaHumanPaletteItemPath& ItemPath) const;

	/** 
	 * Returns true if this struct contains *only* the build output for the given item and its 
	 * sub-items. 
	 * 
	 * This is used to validate the output of item pipelines.
	 */
	METAHUMANCHARACTERPALETTE_API bool ContainsOnlyValidBuildOutputForItem(const FMetaHumanPaletteItemPath& ItemPath) const;

#if WITH_EDITOR
	/**
	 * Move the build output from an item into this struct.
	 * 
	 * Call ContainsOnlyValidBuildOutputForItem on the source data first, to ensure it's valid.
	 * Passing invalid source data into this function will cause an assertion to fail.
	 */
	METAHUMANCHARACTERPALETTE_API void IntegrateItemBuiltData(const FMetaHumanPaletteItemPath& SourceItemPath, FName SourceItemSlotName, FMetaHumanPaletteBuiltData&& SourceItemBuiltData);
#endif // WITH_EDITOR

	/** 
	 * The built data for each item in the palette, including items nested within other items.
	 * 
	 * The key is the path to the item *from the owning Collection*. In other words, it's the 
	 * absolute path to the item, rather than the relative path from this palette.
	 * 
	 * The output for the Collection itself is stored with the empty item path as the key. SlotName
	 * will be ignored for this entry.
	 * 
	 * In future, we will improve the encapsulation of item built data by making this private and
	 * only allowing an item's pipeline to access data belonging to that item.
	 */
	UPROPERTY()
	FMetaHumanPipelineBuiltDataCollection ItemBuiltData;
};

/** Caches data that is expensive to rebuild */
struct FMetaHumanPaletteBuildCacheEntry
{
	UE_DEPRECATED(5.8, "FMetaHumanPaletteBuildCacheEntry is unused and will be removed")
	FInstancedStruct CachedData;
};

/** 
 * Base class for objects that can contain items targeting a Character Pipeline.
 */
UCLASS(Abstract)
class METAHUMANCHARACTERPALETTE_API UMetaHumanCharacterPalette : public UObject
{
	GENERATED_BODY()

public:

#if WITH_EDITOR
	/** Creates a new internal Wardrobe Item for a principal asset and adds it to the palette. */
	[[nodiscard]] bool TryAddItemFromPrincipalAsset(FName SlotName, const FSoftObjectPath& PrincipalAsset, FMetaHumanPaletteItemKey& OutNewItemKey);

	/** 
	 * Adds a Wardrobe Item to the palette. 
	 * 
	 * The Wardrobe Item must be a self-contained asset, rather than an internal Wardrobe Item from 
	 * another palette. 
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Wardrobe")
	[[nodiscard]] bool TryAddItemFromWardrobeItem(FName SlotName, UMetaHumanWardrobeItem* WardrobeItem, FMetaHumanPaletteItemKey& OutNewItemKey);

	/** 
	 * Adds an item to this palette, if there's no existing item with the same key.
	 * 
	 * Prefer to call TryAddItemFromPrincipalAsset or TryAddItemFromWardrobeItem if possible, as
	 * those handle setting up the item correctly for you.
	 * 
	 * This function is guaranteed to succeed if the item's Variation is set from a call to 
	 * GenerateUniqueVariationName.
	 */
	[[nodiscard]] bool TryAddItem(const FMetaHumanCharacterPaletteItem& NewItem);

	/**
	 * Removes a single item from the palette for the given key. Returns boolean indicating
	 * success or failure of the operation.
	 */
	[[nodiscard]] virtual bool TryRemoveItem(const FMetaHumanPaletteItemKey& ExistingKey);

	/**
	 * Update fields on the item that don't contribute to the palette build.
	 *
	 * Doesn't invalidate the build.
	 */
	[[nodiscard]] bool TryUpdateItemNonBuildProperties(const FMetaHumanPaletteItemKey& Key, const FMetaHumanCharacterPaletteItem& Source);

	/** 
	 * Replace an existing item with a new item.
	 * 
	 * The new item is allowed to have a different item key.
	 * 
	 * The list of items is ordered, and the new item will be put at the same index as the existing
	 * item it replaces. In other words, the existing item will be overwritten in place by the new
	 * one.
	 * 
	 * If the new item's key conflicts with another item in the palette, this operation will fail.
	 * It's guaranteed to succeed if an item matching ExistingKey exists, and the new item's 
	 * Variation is set from a call to GenerateUniqueVariationName.
	 */
	[[nodiscard]] bool TryReplaceItem(const FMetaHumanPaletteItemKey& ExistingKey, const FMetaHumanCharacterPaletteItem& NewItem);

	/** 
	 * Remove all items assigned to the given slot
	 * 
	 * Returns the number of items that were removed.
	 */
	int32 RemoveAllItemsForSlot(FName SlotName);

	/** 
	 * If this palette already contains an item matching the SourceKey, generate a variation name 
	 * that doesn't match any existing item.
	 * 
	 * If there is no matching item, simply returns SourceKey's Variation.
	 */
	FName GenerateUniqueVariationName(const FMetaHumanPaletteItemKey& SourceKey) const;

	virtual const UMetaHumanCharacterEditorPipeline* GetPaletteEditorPipeline() const
		PURE_VIRTUAL(UMetaHumanCharacterPalette::GetPaletteEditorPipeline, return nullptr;);
#endif
	
	/** Returns true if an item with the given key exists in this palette */
	bool ContainsItem(const FMetaHumanPaletteItemKey& Key) const;

	/** Fetches the item with the given key, if it exists */
	[[nodiscard]] bool TryFindItem(const FMetaHumanPaletteItemKey& Key, FMetaHumanCharacterPaletteItem& OutItem) const;

	/** Try to find the item and its containing palette referenced by the given path. */
	[[nodiscard]] bool TryResolveItem(const FMetaHumanPaletteItemPath& ItemPath, const UMetaHumanCharacterPalette*& OutContainingPalette, FMetaHumanCharacterPaletteItem& OutItem) const;

	/** 
	 * Try to find the pipeline corresponding to the given item path.
	 * 
	 * Note that an empty item path refers to the Collection itself.
	 */
	[[nodiscard]] bool TryResolvePipeline(const FMetaHumanPaletteItemPath& ItemPath, const UMetaHumanCharacterPipeline*& OutPipeline) const;

	/**
	 * Same as TryResolvePipeline, except that it can only resolve item pipelines, not the 
	 * Collection's pipeline.
	 * 
	 * This is convenient if you know the path corresponds to an item, because it saves you from
	 * having to cast the return value to UMetaHumanItemPipeline.
	 */
	[[nodiscard]] bool TryResolveItemPipeline(const FMetaHumanPaletteItemPath& ItemPath, const UMetaHumanItemPipeline*& OutPipeline) const;

	/** Provides const access to the items array */
	const TArray<FMetaHumanCharacterPaletteItem>& GetItems() const;
	
	virtual const UMetaHumanCharacterPipeline* GetPalettePipeline() const
		PURE_VIRTUAL(UMetaHumanCharacterPalette::GetPalettePipeline, return nullptr;);

protected:
#if WITH_EDITOR
	/** 
	 * Copies the contents of the Other palette into this palette, discarding this palette's
	 * existing contents.
	 * 
	 * Used for setting up a palette to match another palette as closely as possible.
	 * 
	 * Should give a near-identical result to DuplicateObject on Other, except that it updates 
	 * this object in place instead of creating a new object.
	 */
	void CopyContentsFromPalette(TNotNull<const UMetaHumanCharacterPalette*> Other);

	/**
	 * Called when any of the palette's items are modified, e.g. when items are added or removed.
	 * 
	 * This allows subclasses to rebuild any derived data.
	 */
	virtual void OnItemsModified();

	/**
	 * Called by TryAddItemFromWardrobeItem when TestWardrobeItemCompatibilityWithSlot returns Import.
	 *
	 * Subclasses that support importing Wardrobe Items with alternative pipelines should override
	 * this to delegate to their Collection Editor Pipeline's TryCreateItemForImport.
	 *
	 * @return true if the item was imported and added to the palette
	 */
	virtual bool TryImportWardrobeItem(FName SlotName, TNotNull<const UMetaHumanWardrobeItem*> SourceWardrobeItem, FMetaHumanPaletteItemKey& OutNewItemKey);
#endif // WITH_EDITOR

	/**
	 * The items contained in this palette.
	 * 
	 * Each item must have a unique Item Key (returned by GetItemKey) within this palette. 
	 * 
	 * This list is ordered. Operations on the list must not arbitrarily reorder it, e.g. by use 
	 * of RemoveSwap.
	 */
	UPROPERTY()
	TArray<FMetaHumanCharacterPaletteItem> Items;

private:
#if WITH_EDITOR
	void AddItemFromKnownCompatibleWardrobeItem(FName SlotName, TNotNull<UMetaHumanWardrobeItem*> WardrobeItem, FMetaHumanPaletteItemKey& OutNewItemKey);
#endif
};
