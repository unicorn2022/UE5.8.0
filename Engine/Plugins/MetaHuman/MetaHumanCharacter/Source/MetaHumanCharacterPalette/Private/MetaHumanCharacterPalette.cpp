// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterPalette.h"

#include "MetaHumanCharacterEditorPipeline.h"
#include "MetaHumanInstance.h"
#include "MetaHumanCharacterPaletteLog.h"
#include "MetaHumanCharacterPaletteProjectSettings.h"
#include "MetaHumanCharacterPipelineSpecification.h"
#include "MetaHumanItemPipeline.h"
#include "MetaHumanPaletteItemPath.h"
#include "MetaHumanWardrobeItem.h"
#include "MetaHumanCollectionPipeline.h"

#include "Logging/StructuredLog.h"

#if WITH_EDITORONLY_DATA
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#endif

bool FMetaHumanPaletteBuiltData::HasBuildOutputForItem(const FMetaHumanPaletteItemPath& ItemPath) const
{
	// Items must produce their own build output
	const FMetaHumanPipelineBuiltData* BuiltData = ItemBuiltData.View().Find(ItemPath);
	if (!BuiltData)
	{
		return false;
	}

	return BuiltData->BuildOutput.IsValid();
}

bool FMetaHumanPaletteBuiltData::ContainsOnlyValidBuildOutputForItem(const FMetaHumanPaletteItemPath& ItemPath) const
{
	if (!ItemBuiltData.View().Contains(ItemPath))
	{
		// Items must produce build output for themselves
		return false;
	}

	for (const FMetaHumanPipelineBuiltDataCollectionPair& Pair : ItemBuiltData.View().SortedElements)
	{
		if (!Pair.Key.IsEqualOrChildPathOf(ItemPath))
		{
			// Items may not produce build output for items outside of their own path
			return false;
		}

		if (Pair.Value.SlotName == NAME_None
			&& Pair.Key != ItemPath)
		{
			// Invalid slot name for item
			//
			// The base item is allowed to have an empty slot name, because the item pipeline
			// doesn't know which slot the item is in.
			return false;
		}

		if (!Pair.Value.BuildOutput.IsValid())
		{
			// Build output struct must be valid
			return false;
		}
	}

	return true;
}

#if WITH_EDITOR

void FMetaHumanPaletteBuiltData::IntegrateItemBuiltData(const FMetaHumanPaletteItemPath& SourceItemPath, FName SourceItemSlotName, FMetaHumanPaletteBuiltData&& SourceItemBuiltData)
{
	check(SourceItemBuiltData.ContainsOnlyValidBuildOutputForItem(SourceItemPath));

	FMetaHumanPipelineBuiltData& ItemOwnBuiltData = SourceItemBuiltData.ItemBuiltData.MutableView()[SourceItemPath];
	// The item pipeline doesn't know which slot the item is in, so we write that here.
	ItemOwnBuiltData.SlotName = SourceItemSlotName;

	ItemBuiltData.Edit().Append(SourceItemBuiltData.ItemBuiltData.Edit());
}

bool UMetaHumanCharacterPalette::TryAddItem(const FMetaHumanCharacterPaletteItem& NewItem)
{
	if (ContainsItem(NewItem.GetItemKey()))
	{
		return false;
	}

	Items.Add(NewItem);
	OnItemsModified();

	return true;
}

bool UMetaHumanCharacterPalette::TryAddItemFromPrincipalAsset(FName SlotName, const FSoftObjectPath& PrincipalAsset, FMetaHumanPaletteItemKey& OutNewItemKey)
{
	const UObject* LoadedPrincipalAsset = PrincipalAsset.TryLoad();

	if (!LoadedPrincipalAsset
		|| !GetPaletteEditorPipeline()
		|| !GetPaletteEditorPipeline()->IsPrincipalAssetClassCompatibleWithSlot(SlotName, LoadedPrincipalAsset->GetClass()))
	{
		// Asset doesn't exist, or the slot doesn't support this asset type
		return false;
	}

	UMetaHumanWardrobeItem* WardrobeItem = NewObject<UMetaHumanWardrobeItem>(this);
	WardrobeItem->PrincipalAsset = PrincipalAsset;

	AddItemFromKnownCompatibleWardrobeItem(SlotName, WardrobeItem, OutNewItemKey);
	return true;
}

bool UMetaHumanCharacterPalette::TryAddItemFromWardrobeItem(FName SlotName, UMetaHumanWardrobeItem* WardrobeItem, FMetaHumanPaletteItemKey& OutNewItemKey)
{
	if (!WardrobeItem)
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "TryAddItemFromWardrobeItem called with invalid wardrobe item for slot '{SlotName}'", SlotName.ToString());
		return false;
	}

	if (!WardrobeItem->IsExternal())
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "Trying to add wardrobe item '{WardrobeItem}' that is not external for slot '{SlotName}'. Palettes can't directly reference Wardrobe Items that belong to other palettes", WardrobeItem->GetName(),  SlotName.ToString());
		return false;
	}
	
	if (WardrobeItem->PrincipalAsset.IsNull())
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "TryAddItemFromWardrobeItem called with wardrobe item that has no principal asset for slot '{SlotName}'", SlotName.ToString());
		return false;
	}

	if (!GetPaletteEditorPipeline())
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "Can't add items to {Palette}, as no pipeline is set", GetPathName());
		return false;
	}

	const EMetaHumanWardrobeItemCompatibility Compatibility = GetPaletteEditorPipeline()->TestWardrobeItemCompatibilityWithSlot(SlotName, WardrobeItem);

	switch (Compatibility)
	{
	case EMetaHumanWardrobeItemCompatibility::Add:
		AddItemFromKnownCompatibleWardrobeItem(SlotName, WardrobeItem, OutNewItemKey);
		return true;

	case EMetaHumanWardrobeItemCompatibility::Import:
		return TryImportWardrobeItem(SlotName, WardrobeItem, OutNewItemKey);

	default:
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "Slot '{SlotName}' doesn't support asset type of Wardrobe Item '{WardrobeItem}'", SlotName.ToString(), WardrobeItem->GetName());
		return false;
	}
}

bool UMetaHumanCharacterPalette::TryRemoveItem(const FMetaHumanPaletteItemKey& ExistingKey)
{
	const int32 ExistingIndex = Items.IndexOfByPredicate([&ExistingKey](const FMetaHumanCharacterPaletteItem& ExistingItem)
		{
			return ExistingItem.GetItemKey() == ExistingKey;
		});

	if (ExistingIndex == INDEX_NONE)
	{
		// Key not found in items array
		return false;
	}

	Items.RemoveAt(ExistingIndex);
	OnItemsModified();

	return true;
}

bool UMetaHumanCharacterPalette::TryReplaceItem(const FMetaHumanPaletteItemKey& ExistingKey, const FMetaHumanCharacterPaletteItem& NewItem)
{
	const int32 ExistingIndex = Items.IndexOfByPredicate([&ExistingKey](const FMetaHumanCharacterPaletteItem& ExistingItem)
		{
			return ExistingItem.GetItemKey() == ExistingKey;
		});

	if (ExistingIndex == INDEX_NONE)
	{
		// Key not found in items array
		return false;
	}

	const FMetaHumanPaletteItemKey NewKey = NewItem.GetItemKey();
	if (NewKey != ExistingKey && ContainsItem(NewKey))
	{
		// Can't change the item key to one that already exists
		return false;
	}

	Items[ExistingIndex] = NewItem;
	OnItemsModified();

	return true;
}

bool UMetaHumanCharacterPalette::TryUpdateItemNonBuildProperties(const FMetaHumanPaletteItemKey& Key, const FMetaHumanCharacterPaletteItem& Source)
{
	const int32 ExistingIndex = Items.IndexOfByPredicate([&Key](const FMetaHumanCharacterPaletteItem& ExistingItem)
		{
			return ExistingItem.GetItemKey() == Key;
		});

	if (ExistingIndex == INDEX_NONE)
	{
		return false;
	}

	// Only copy fields that don't contribute to the palette build
#if WITH_EDITORONLY_DATA
	Items[ExistingIndex].DisplayName = Source.DisplayName;
#endif

	MarkPackageDirty();

	return true;
}

int32 UMetaHumanCharacterPalette::RemoveAllItemsForSlot(FName SlotName)
{
	const int32 NumRemoved = Items.RemoveAll([SlotName](const FMetaHumanCharacterPaletteItem& Item)
		{
			return Item.SlotName == SlotName;
		});

	if (NumRemoved > 0)
	{
		OnItemsModified();
	}

	return NumRemoved;
}

FName UMetaHumanCharacterPalette::GenerateUniqueVariationName(const FMetaHumanPaletteItemKey& SourceKey) const
{
	// Items that have the same principal asset as the passed in key
	TArray<FName, TInlineAllocator<8>> MatchingItemVariations;

	bool bFoundExactMatch = false;
	for (const FMetaHumanCharacterPaletteItem& ExistingItem : Items)
	{
		const FMetaHumanPaletteItemKey ExistingKey = ExistingItem.GetItemKey();
		if (ExistingKey.ReferencesSameAsset(SourceKey))
		{
			MatchingItemVariations.Add(ExistingKey.Variation);

			if (ExistingKey == SourceKey)
			{
				bFoundExactMatch = true;
			}
		}
	}

	if (!bFoundExactMatch)
	{
		// SourceKey doesn't match any existing items
		return SourceKey.Variation;
	}

	// Find a variation name that doesn't conflict with an existing item

	// Start generating variations at 2, so that we get "Asset", "Asset 2", "Asset 3", etc as 
	// generated names, without using "Asset 1".
	FName NewVariation = SourceKey.Variation;
	if (NewVariation.GetNumber() == 0)
	{
		NewVariation.SetNumber(1);
	}

	const int32 OriginalVariationNumber = FMath::Clamp(NewVariation.GetNumber(), 0, MAX_int32);
	int32 VariationNumber = OriginalVariationNumber;
	while (true)
	{
		// Keep the number non-negative
		if (VariationNumber == MAX_int32)
		{
			VariationNumber = 0;
		}

		VariationNumber++;

		if (VariationNumber == OriginalVariationNumber)
		{
			// We have tried all possible variations and failed to find an unused one.
			//
			// Given that the variation number is 32 bits, this should never happen in practice.
			checkNoEntry();

			// If continuing past the assert, return the original name just to avoid an infinite 
			// loop.
			return SourceKey.Variation;
		}

		NewVariation.SetNumber(VariationNumber);

		if (!MatchingItemVariations.Contains(NewVariation))
		{
			break;
		}
	}

	return NewVariation;
}
#endif // WITH_EDITOR

const TArray<FMetaHumanCharacterPaletteItem>& UMetaHumanCharacterPalette::GetItems() const
{
	return Items;
}

bool UMetaHumanCharacterPalette::ContainsItem(const FMetaHumanPaletteItemKey& Key) const
{
	return Items.ContainsByPredicate([&Key](const FMetaHumanCharacterPaletteItem& Item)
	{
		return Item.GetItemKey() == Key;
	});
}

bool UMetaHumanCharacterPalette::TryFindItem(const FMetaHumanPaletteItemKey& Key, FMetaHumanCharacterPaletteItem& OutItem) const
{
	const FMetaHumanCharacterPaletteItem* FoundItem = Items.FindByPredicate([&Key](const FMetaHumanCharacterPaletteItem& Item)
		{
			return Item.GetItemKey() == Key;
		});

	if (FoundItem == nullptr)
	{
		// Item not found in items array
		return false;
	}

	OutItem = *FoundItem;
	return true;
}

bool UMetaHumanCharacterPalette::TryResolveItem(const FMetaHumanPaletteItemPath& ItemPath, const UMetaHumanCharacterPalette*& OutContainingPalette, FMetaHumanCharacterPaletteItem& OutItem) const
{
	OutContainingPalette = nullptr;
	OutItem = FMetaHumanCharacterPaletteItem();

	if (ItemPath.IsEmpty())
	{
		return false;
	}

	if (ItemPath.GetNumPathEntries() > 1)
	{
		// TODO: Support nested palettes
		unimplemented();
		return false;
	}

	const FMetaHumanPaletteItemKey ItemKey = ItemPath.GetPathEntry(0);
	if (!TryFindItem(ItemKey, OutItem))
	{
		return false;
	}

	OutContainingPalette = this;
	return true;
}

bool UMetaHumanCharacterPalette::TryResolvePipeline(const FMetaHumanPaletteItemPath& ItemPath, const UMetaHumanCharacterPipeline*& OutPipeline) const
{
	OutPipeline = nullptr;

	if (ItemPath.IsEmpty())
	{
		OutPipeline = GetPalettePipeline();
		return OutPipeline != nullptr;
	}

	const UMetaHumanCharacterPalette* ContainingPalette = nullptr;
	FMetaHumanCharacterPaletteItem Item;
	if (!TryResolveItem(ItemPath, ContainingPalette, Item))
	{
		return false;
	}

	if (!Item.WardrobeItem)
	{
		return false;
	}

	OutPipeline = Item.WardrobeItem->GetPipeline();

	// Principal assets aren't guaranteed to be cooked, so we can't rely on loading them outside 
	// the editor.
	//
	// This should be refactored to work outside the editor, or the whole function should be made
	// editor-only.
#if WITH_EDITORONLY_DATA
	if (OutPipeline == nullptr)
	{
		if (const UMetaHumanCollectionPipeline* CollectionPipeline = Cast<UMetaHumanCollectionPipeline>(GetPalettePipeline()))
		{
			if (UObject* PrincipalAsset = Item.LoadPrincipalAssetSynchronous())
			{
				OutPipeline = CollectionPipeline->GetFallbackItemPipelineForAssetType(PrincipalAsset->GetClass());
			}
		}
	}
#endif // WITH_EDITORONLY_DATA

	return OutPipeline != nullptr;
}

bool UMetaHumanCharacterPalette::TryResolveItemPipeline(const FMetaHumanPaletteItemPath& ItemPath, const UMetaHumanItemPipeline*& OutPipeline) const
{
	const UMetaHumanCharacterPipeline* CharacterPipeline = nullptr;
	if (!TryResolvePipeline(ItemPath, CharacterPipeline))
	{
		return false;
	}

	OutPipeline = Cast<UMetaHumanItemPipeline>(CharacterPipeline);
	return OutPipeline != nullptr;
}

#if WITH_EDITOR
void UMetaHumanCharacterPalette::CopyContentsFromPalette(TNotNull<const UMetaHumanCharacterPalette*> Other)
{
	Items.Reset();

	for (const FMetaHumanCharacterPaletteItem& OtherItem : Other->Items)
	{
		FMetaHumanCharacterPaletteItem& CopiedItem = Items.Add_GetRef(OtherItem);
		if (CopiedItem.WardrobeItem
			&& !CopiedItem.WardrobeItem->IsExternal())
		{
			// The Wardrobe Item is a subobject of the other palette, so it must be duplicated into this palette
			CopiedItem.WardrobeItem = DuplicateObject<UMetaHumanWardrobeItem>(CopiedItem.WardrobeItem, this);
		}
	}

	OnItemsModified();
}

void UMetaHumanCharacterPalette::OnItemsModified()
{
	// Base implementation does nothing
}

void UMetaHumanCharacterPalette::AddItemFromKnownCompatibleWardrobeItem(FName SlotName, TNotNull<UMetaHumanWardrobeItem*> WardrobeItem, FMetaHumanPaletteItemKey& OutNewItemKey)
{
	check(WardrobeItem->PrincipalAsset.IsValid());

	OutNewItemKey.Reset();

	TSharedRef<FMetaHumanCharacterPaletteItem> NewItem = MakeShared<FMetaHumanCharacterPaletteItem>();
	NewItem->WardrobeItem = WardrobeItem;
	NewItem->SlotName = SlotName;

	// Ensure new item key is unique
	NewItem->Variation = GenerateUniqueVariationName(NewItem->GetItemKey());

	NewItem->DisplayName = NewItem->GetOrGenerateDisplayName();

	// The add operation should succeed because the key should be unique
	verify(TryAddItem(NewItem.Get()));

	OutNewItemKey = NewItem->GetItemKey();
}

bool UMetaHumanCharacterPalette::TryImportWardrobeItem(FName SlotName, TNotNull<const UMetaHumanWardrobeItem*> SourceWardrobeItem, FMetaHumanPaletteItemKey& OutNewItemKey)
{
	return false;
}
#endif // WITH_EDITOR
