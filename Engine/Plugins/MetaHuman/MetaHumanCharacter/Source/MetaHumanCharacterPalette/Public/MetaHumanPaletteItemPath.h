// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanPaletteItemKey.h"

#include "MetaHumanPaletteItemPath.generated.h"

#define UE_API METAHUMANCHARACTERPALETTE_API

/** 
 * Represents the path to a Wardrobe Item within a Collection.
 * 
 * Items can contain other items, and a FMetaHumanPaletteItemKey is only unique within the 
 * Collection or Wardrobe Item it belongs to, so to address a unique item takes a sequence of
 * FMetaHumanPaletteItemKeys: one for each level of nesting.
 */
USTRUCT(BlueprintType, meta = (HasNativeMake="/Script/MetaHumanCharacterPalette.MetaHumanPaletteItemPathBlueprintLibrary.MakeItemPath"))
struct FMetaHumanPaletteItemPath
{
	GENERATED_BODY()

public:
	UE_API FMetaHumanPaletteItemPath();
	UE_API ~FMetaHumanPaletteItemPath();
	UE_API FMetaHumanPaletteItemPath(const FMetaHumanPaletteItemPath&);
	UE_API FMetaHumanPaletteItemPath(FMetaHumanPaletteItemPath&&);
	UE_API FMetaHumanPaletteItemPath& operator=(const FMetaHumanPaletteItemPath&);
	UE_API FMetaHumanPaletteItemPath& operator=(FMetaHumanPaletteItemPath&&);
	UE_API explicit FMetaHumanPaletteItemPath(const FMetaHumanPaletteItemKey& InItem);
	UE_API FMetaHumanPaletteItemPath(const TArray<FMetaHumanPaletteItemKey>& InParentItems, const FMetaHumanPaletteItemKey& InItem);
	UE_API FMetaHumanPaletteItemPath(const FMetaHumanPaletteItemPath& InParentItemPath, const FMetaHumanPaletteItemKey& InItem);

	/** 
	 * In many contexts, the empty item path represents the path to the containing Collection 
	 * itself.
	 * 
	 * This constant contains the empty path to help make it explicit that a caller is referencing
	 * the Collection, as opposed to using a default-constructed path.
	 */
	UE_API static const FMetaHumanPaletteItemPath Collection;

	UE_API bool IsEmpty() const;

	UE_API int32 GetNumPathEntries() const;

	/** Index must be in the range 0 .. GetNumPathEntries() - 1, otherwise an assertion will fail. */
	UE_API FMetaHumanPaletteItemKey GetPathEntry(int32 Index) const;

	/** 
	 * Compares this path to the given path and returns true if this path contains exactly one more
	 * entry, and the other entries exactly match the given path.
	 * 
	 * For example, using filesystem syntax "/A/B/C".IsDirectChildPathOf("/A/B") would return true.
	 */
	UE_API bool IsDirectChildPathOf(const FMetaHumanPaletteItemPath& ParentPath) const;

	/** 
	 * Returns true if this path is equal to the other path, or is a child path of it.
	 * 
	 * This is useful when filtering a list of item paths to include only paths that relate to a 
	 * particular item and its sub-items.
	 * 
	 * For example, given a list of item paths, if the expression X.IsEqualOrChildPathOf(ItemToFilter)
	 * returns true for a path X from the list, it's either ItemToFilter itself or one of its 
	 * sub-items (or sub-sub-items, etc).
	 */
	UE_API bool IsEqualOrChildPathOf(const FMetaHumanPaletteItemPath& ParentPath) const;

	UE_API void Append(const FMetaHumanPaletteItemPath& PathToAppend);

	UE_API FString ToDebugString() const;

	/**
	 * Compare this path with the other path to determine their sort order.
	 * 
	 * Returns -1 if this path is "less" than the other, 1 if it's "more", and 0 if they're equal.
	 */
	UE_API int32 Compare(const FMetaHumanPaletteItemPath& Other) const;

	// Allow the compiler to generate default equality operations
	UE_API friend bool operator==(const FMetaHumanPaletteItemPath&, const FMetaHumanPaletteItemPath&) = default;
	UE_API friend bool operator!=(const FMetaHumanPaletteItemPath&, const FMetaHumanPaletteItemPath&) = default;

	// A fast less-than operator for sorting that is *not* stable across engine instances, i.e. if 
	// the results are written to disk and loaded by another instance of the engine, the sort order
	// may change even though the sorted elements are the same.
	UE_API friend bool operator<(const FMetaHumanPaletteItemPath& A, const FMetaHumanPaletteItemPath& B);

	UE_API friend uint32 GetTypeHash(const FMetaHumanPaletteItemPath& ItemKey);

private:
	/** 
	 * The sequence of parent items to reach the actual item referenced by this path.
	 * 
	 * For the simple case where this path references a Wardrobe Item that is directly contained in a 
	 * Collection, with no item nesting, this array will be empty.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Character")
	TArray<FMetaHumanPaletteItemKey> ParentItems;

	/** The last item in the path */
	UPROPERTY(VisibleAnywhere, Category = "Character")
	FMetaHumanPaletteItemKey Item;
};

#undef UE_API
