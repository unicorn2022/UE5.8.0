// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCollectionEditorPipeline.h"

#include "MetaHumanCharacterPaletteLog.h"
#include "MetaHumanCollection.h"
#include "MetaHumanCollectionPipeline.h"
#include "MetaHumanItemEditorPipeline.h"
#include "MetaHumanItemPipeline.h"
#include "MetaHumanWardrobeItem.h"
#include "MetaHumanUnpackUtilities.h"

#include "Engine/BlueprintGeneratedClass.h"
#include "Logging/StructuredLog.h"
#include "UObject/Package.h"

#if WITH_EDITOR

void UMetaHumanCollectionEditorPipeline::BuildCollection(
	TNotNull<const UMetaHumanCollection*> Collection,
	TNotNull<UObject*> OuterForGeneratedAssets,
	const TArray<FMetaHumanPinnedSlotSelection>& SortedPinnedSlotSelections,
	const TArray<FMetaHumanPaletteItemPath>& SortedItemsToExclude,
	const FInstancedStruct& BuildInput,
	EMetaHumanCharacterPaletteBuildQuality Quality,
	ITargetPlatform* TargetPlatform,
	const FOnBuildComplete& OnComplete) const
{
	const FBuildCollectionParams Params
	{
		.Collection = Collection,
		.OuterForGeneratedAssets = OuterForGeneratedAssets,
		.SortedPinnedSlotSelections = MakeConstArrayView(SortedPinnedSlotSelections),
		.SortedItemsToExclude = MakeConstArrayView(SortedItemsToExclude),
		.BuildInput = BuildInput
	};

	BuildCollection(Params, OnComplete);
}

void UMetaHumanCollectionEditorPipeline::UnpackCollectionAssets(
	TNotNull<const UMetaHumanCollection*> Collection,
	FMetaHumanCollectionBuiltData& CollectionBuiltData,
	const FOnUnpackComplete& OnComplete) const
{
	if (!CollectionBuiltData.IsValid())
	{
		OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed);
		return;
	}

	check(Collection->GetOuter()->IsA<UPackage>());

	TSet<UObject*> AllAssetsToUnpack = UE::MetaHuman::UnpackUtilities::GetDirectSubobjectsOfOwnerFromStruct(
		FMetaHumanCollectionBuiltData::StaticStruct(), 
		&CollectionBuiltData,
		Collection,
		/* bRecursive */ true);

	// BPGCs are explicitly excluded, because only the corresponding blueprints should be moved.
	//
	// The blueprints will handle updating their generated classes themselves.
	for (TSet<UObject*>::TIterator It = AllAssetsToUnpack.CreateIterator(); It; ++It)
	{
		const UObject* DirectSubobjectOfCollection = *It;

		if (DirectSubobjectOfCollection->IsA<UBlueprintGeneratedClass>()
			|| DirectSubobjectOfCollection->HasAnyFlags(RF_ClassDefaultObject))
		{
			It.RemoveCurrent();
		}
	}

	const FString UnpackFolder = Collection->GetUnpackFolder();

	// The paths of all unpacked assets, so that we can ensure we don't unpack two different 
	// assets to the same path.
	TSet<FString> UnpackedAssetPaths;

	// Pre-processing pass: for each item (including sub-items), discover the assets referenced from
	// its built data and compute a fallback unpack path based on the item name. These are used 
	// during the failsafe catch-all at the end of this function to give a meaningful unpack path to
	// any assets that weren't unpacked by their item's pipeline or explicit metadata.
	//
	// This pass is needed to avoid incorrectly attributing assets to the wrong item when one 
	// item's built data references an asset owned by another item (e.g. a groom referencing a head
	// mesh).
	TMap<UObject*, FString> FallbackUnpackPaths;
	{
		// Pass 1: collect all objects explicitly claimed by any item's metadata.
		TMap<UObject*, FMetaHumanPaletteItemPath> MetadataClaimedObjects;
		for (const FMetaHumanPipelineBuiltDataCollectionPair& Item : CollectionBuiltData.PaletteBuiltData.ItemBuiltData.View().SortedElements)
		{
			for (const FMetaHumanGeneratedAssetMetadata& AssetMetadata : Item.Value.Metadata)
			{
				if (AssetMetadata.Object)
				{
					MetadataClaimedObjects.Add(AssetMetadata.Object, Item.Key);
				}
			}
		}

		// Pass 2: for each item, discover referenced assets, but don't follow references through
		// objects claimed by other items' metadata.
		for (const FMetaHumanPipelineBuiltDataCollectionPair& Item : CollectionBuiltData.PaletteBuiltData.ItemBuiltData.View().SortedElements)
		{
			const FInstancedStruct& BuildOutput = Item.Value.BuildOutput;
			if (!BuildOutput.IsValid())
			{
				continue;
			}

			// Get the non-recursive (direct) references from this item's built data.
			TSet<UObject*> DirectRefs = UE::MetaHuman::UnpackUtilities::GetDirectSubobjectsOfOwnerFromStruct(
				BuildOutput.GetScriptStruct(),
				BuildOutput.GetMemory(),
				Collection,
				/* bRecursive */ false);

			// Filter out objects claimed by other items' metadata, so we don't follow references
			// through them and incorrectly attribute their dependencies to this item.
			TArray<UObject*> RootsToExpand;
			for (UObject* Ref : DirectRefs)
			{
				const FMetaHumanPaletteItemPath* ClaimingItem = MetadataClaimedObjects.Find(Ref);
				if (ClaimingItem && *ClaimingItem != Item.Key)
				{
					// This object is explicitly claimed by another item's metadata; skip it.
					continue;
				}

				RootsToExpand.Add(Ref);
			}

			// Recursively expand from the unclaimed roots to discover transitive dependencies.
			TSet<UObject*> ItemAssets = UE::MetaHuman::UnpackUtilities::GetDirectSubobjectsOfOwnerFromRoots(
				RootsToExpand,
				Collection,
				/* bRecursive */ true);

			// Also include the direct roots themselves.
			ItemAssets.Append(TSet<UObject*>(RootsToExpand));

			// Determine the base path for fallback unpacking of this item's assets.
			FString ItemUnpackBase;
			if (!Item.Value.DefaultUnpackSubfolder.IsEmpty())
			{
				if (Item.Value.bDefaultSubfolderIsAbsolute)
				{
					ItemUnpackBase = Item.Value.DefaultUnpackSubfolder;
				}
				else
				{
					ItemUnpackBase = UnpackFolder / Item.Value.DefaultUnpackSubfolder;
				}
			}
			else if (Item.Key == FMetaHumanPaletteItemPath::Collection)
			{
				// The collection's own assets unpack to a subfolder called Collection by default
				ItemUnpackBase = UnpackFolder / TEXT("Collection");
			}
			else
			{
				const FString ItemSubfolder = Item.Key.GetPathEntry(Item.Key.GetNumPathEntries() - 1).ToAssetNameString();
				ItemUnpackBase = UnpackFolder / ItemSubfolder;
			}

			for (UObject* Asset : ItemAssets)
			{
				// Only add a fallback path for objects that don't already have one, so that the first
				// item to reference an asset "wins".
				if (!FallbackUnpackPaths.Contains(Asset))
				{
					FallbackUnpackPaths.Add(Asset, ItemUnpackBase / Asset->GetName());
				}
			}
		}
	}

	// Now invoke the item pipelines to perform any bespoke unpacking operations, and unpack any
	// assets with explicit metadata entries. 
	for (const FMetaHumanPipelineBuiltDataCollectionPair& Item : CollectionBuiltData.PaletteBuiltData.ItemBuiltData.View().SortedElements)
	{
		// Only process items directly owned by the collection, i.e. not sub-items
		if (Item.Key != FMetaHumanPaletteItemPath::Collection 
			&& !Item.Key.IsDirectChildPathOf(FMetaHumanPaletteItemPath::Collection))
		{
			continue;
		}

		if (Item.Key == FMetaHumanPaletteItemPath::Collection)
		{
			// Collections that want to do their own unpacking can override UnpackCollectionAssets 
			// and then call Super::UnpackCollectionAssets to unpack any remaining assets.

			if (!UE::MetaHuman::UnpackUtilities::TryUnpackItemAssetsFromMetaData(
				Item.Key,
				CollectionBuiltData.PaletteBuiltData.ItemBuiltData.MutableView().FilterByBasePath(Item.Key),
				UnpackFolder,
				FTryUnpackObjectDelegate::CreateWeakLambda(this, 
					[this, Collection, &UnpackedAssetPaths](TNotNull<UObject*> Object, FString& InOutAssetPackageName)
					{
						return UE::MetaHuman::UnpackUtilities::TryUnpackObject(Object, Collection, InOutAssetPackageName, UnpackedAssetPaths);
					})))
			{
				OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed);
				return;
			}
		}
		else
		{
			// Invoke the item's pipeline to do any unpacking it wants to do
		
			const UMetaHumanCharacterPalette* ContainingPalette = nullptr;
			FMetaHumanCharacterPaletteItem ResolvedItem;
			if (!Collection->TryResolveItem(Item.Key, ContainingPalette, ResolvedItem)
				|| !ResolvedItem.WardrobeItem)
			{
				OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed);
				return;
			}

			const UMetaHumanItemEditorPipeline* ItemEditorPipeline = nullptr;
			const UMetaHumanItemPipeline* ItemPipeline = nullptr;
			if (Collection->TryResolveItemPipeline(Item.Key, ItemPipeline))
			{
				ItemEditorPipeline = ItemPipeline->GetEditorPipeline();
			}

			// If no item pipeline was found, use the default behavior to unpack based on metadata
			if (!ItemEditorPipeline)
			{
				ItemEditorPipeline = UMetaHumanItemEditorPipeline::StaticClass()->GetDefaultObject<UMetaHumanItemEditorPipeline>();
			}

			if (!ItemEditorPipeline->TryUnpackItemAssets(
				ResolvedItem.WardrobeItem,
				Item.Key,
				CollectionBuiltData.PaletteBuiltData.ItemBuiltData.MutableView().FilterByBasePath(Item.Key),
				UnpackFolder,
				FTryUnpackObjectDelegate::CreateWeakLambda(this, 
					[this, Collection, &UnpackedAssetPaths](TNotNull<UObject*> Object, FString& InOutAssetPackageName)
					{
						return UE::MetaHuman::UnpackUtilities::TryUnpackObject(Object, Collection, InOutAssetPackageName, UnpackedAssetPaths);
					})))
			{
				OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed);
				return;
			}
		}
	}

	// Unpack any remaining objects that weren't caught by the per-item unpacking above
	for (UObject* Obj : AllAssetsToUnpack)
	{
		if (Obj->GetOuter() != Collection)
		{
			// Already unpacked
			continue;
		}

		FString AssetPackagePath;

		if (const FString* FallbackPath = FallbackUnpackPaths.Find(Obj))
		{
			UE_LOGFMT(LogMetaHumanCharacterPalette, Log, "Unpacking generated object {Obj} using fallback path from referencing item", Obj->GetFullName());
			AssetPackagePath = *FallbackPath;
		}
		else
		{
			UE_LOGFMT(LogMetaHumanCharacterPalette, Log, "Unpacking generated object {Obj} with no metadata", Obj->GetFullName());
			AssetPackagePath = UnpackFolder / Obj->GetName();
		}

		if (!UE::MetaHuman::UnpackUtilities::TryUnpackObject(Obj, Collection, AssetPackagePath, UnpackedAssetPaths))
		{
			OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed);
			return;
		}

		// TryUnpackObject should only return true if the object was unpacked
		check(Obj->GetOuter() != Collection);
	}

	OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Succeeded);
}

bool UMetaHumanCollectionEditorPipeline::TryCreateItemForImport(
	TNotNull<UMetaHumanCollection*> Collection,
	FName SlotName,
	TNotNull<const UMetaHumanWardrobeItem*> SourceWardrobeItem,
	FMetaHumanCharacterPaletteItem& OutItem)
{
	return false;
}

bool UMetaHumanCollectionEditorPipeline::ValidateCollection(TNotNull<UMetaHumanCollection*> Collection)
{
	return true;
}

bool UMetaHumanCollectionEditorPipeline::BeginCharacterEditorAssemble(TNotNull<UMetaHumanCollection*> InCollection, const FString& InCharacterName)
{
	return true;
}

void UMetaHumanCollectionEditorPipeline::EndCharacterEditorAssemble(TNotNull<UMetaHumanCollection*> InCollection)
{
}

TNotNull<const UMetaHumanCollectionPipeline*> UMetaHumanCollectionEditorPipeline::GetRuntimePipeline() const
{
	// The editor pipeline is assumed to be a direct subobject of the runtime pipeline.
	//
	// Pipelines with a different setup can override this function.

	return CastChecked<UMetaHumanCollectionPipeline>(GetOuter());
}

TNotNull<const UMetaHumanCharacterPipeline*> UMetaHumanCollectionEditorPipeline::GetRuntimeCharacterPipeline() const
{
	return CastChecked<UMetaHumanCharacterPipeline>(GetRuntimePipeline());
}

bool UMetaHumanCollectionEditorPipeline::ShouldCookInstanceAsAssembled(TNotNull<const UMetaHumanInstance*> Instance) const
{
	return true;
}

UBlueprint* UMetaHumanCollectionEditorPipeline::WriteActorBlueprint(const FString& InBlueprintPath) const
{
	FWriteBlueprintSettings Settings;
	Settings.BlueprintPath = InBlueprintPath;
	return WriteActorBlueprint(Settings);
}

UBlueprint* UMetaHumanCollectionEditorPipeline::WriteActorBlueprint(const FWriteBlueprintSettings& InWriteBlueprintSettings) const
{
	return nullptr;
}

bool UMetaHumanCollectionEditorPipeline::UpdateActorBlueprint(const UMetaHumanInstance* InCharacterInstance, UBlueprint* InBlueprint) const
{
	return false;
}

#endif // WITH_EDITOR