// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanItemEditorPipeline.h"

#include "MetaHumanCharacterPalette.h"
#include "MetaHumanItemPipeline.h"
#include "MetaHumanUnpackUtilities.h"


#if WITH_EDITOR

void UMetaHumanItemEditorPipeline::BuildItem(
	const FMetaHumanPaletteItemPath& ItemPath,
	TNotNull<const UMetaHumanWardrobeItem*> WardrobeItem,
	const FInstancedStruct& BuildInput,
	TConstArrayView<FMetaHumanPinnedSlotSelection> SortedPinnedSlotSelections,
	TConstArrayView<FMetaHumanPaletteItemPath> SortedItemsToExclude,
	FMetaHumanPaletteBuildCacheEntry& BuildCache,
	EMetaHumanCharacterPaletteBuildQuality Quality,
	ITargetPlatform* TargetPlatform,
	TNotNull<UObject*> OuterForGeneratedObjects,
	const FOnBuildComplete& OnComplete) const
{
	const FBuildItemParams Params
	{
		.ItemPath = ItemPath,
		.WardrobeItem = WardrobeItem,
		.Quality = Quality,
		.OuterForGeneratedObjects = OuterForGeneratedObjects,
		.BuildInput = BuildInput,
		.SortedPinnedSlotSelections = SortedPinnedSlotSelections,
		.SortedItemsToExclude = SortedItemsToExclude
	};

	FMetaHumanPaletteBuiltData BuiltData = BuildItem(Params).GetResult();
	OnComplete.ExecuteIfBound(MoveTemp(BuiltData));
}

void UMetaHumanItemEditorPipeline::BuildItemSynchronous(const FBuildItemParams& Params, FMetaHumanPaletteBuiltData& OutBuiltData) const
{
	OutBuiltData = BuildItem(Params).GetResult();
}

void UMetaHumanItemEditorPipeline::BuildItemSynchronous(
	const FMetaHumanPaletteItemPath& ItemPath,
	TNotNull<const UMetaHumanWardrobeItem*> WardrobeItem,
	const FInstancedStruct& BuildInput,
	TConstArrayView<FMetaHumanPinnedSlotSelection> SortedPinnedSlotSelections,
	TConstArrayView<FMetaHumanPaletteItemPath> SortedItemsToExclude,
	FMetaHumanPaletteBuildCacheEntry& BuildCache,
	EMetaHumanCharacterPaletteBuildQuality Quality,
	ITargetPlatform* TargetPlatform,
	TNotNull<UObject*> OuterForGeneratedObjects,
	FMetaHumanPaletteBuiltData& OutBuiltData) const
{
	const FBuildItemParams Params
	{
		.ItemPath = ItemPath,
		.WardrobeItem = WardrobeItem,
		.Quality = Quality,
		.OuterForGeneratedObjects = OuterForGeneratedObjects,
		.BuildInput = BuildInput,
		.SortedPinnedSlotSelections = SortedPinnedSlotSelections,
		.SortedItemsToExclude = SortedItemsToExclude
	};

	BuildItemSynchronous(Params, OutBuiltData);
}

bool UMetaHumanItemEditorPipeline::TryUnpackItemAssets(
	TNotNull<const UMetaHumanWardrobeItem*> WardrobeItem,
	const FMetaHumanPaletteItemPath& BaseItemPath,
	FMetaHumanPipelineBuiltDataCollectionMutableView ItemBuiltData,
	const FString& UnpackFolder,
	const FTryUnpackObjectDelegate& TryUnpackObjectDelegate) const
{
	// TODO: Unpack sub-items first

	return UE::MetaHuman::UnpackUtilities::TryUnpackItemAssetsFromMetaData(
		BaseItemPath,
		ItemBuiltData,
		UnpackFolder,
		TryUnpackObjectDelegate);
}

TNotNull<const UMetaHumanItemPipeline*> UMetaHumanItemEditorPipeline::GetRuntimePipeline() const
{
	// The editor pipeline is assumed to be a direct subobject of the runtime pipeline.
	//
	// Pipelines with a different setup can override this function.

	return CastChecked<UMetaHumanItemPipeline>(GetOuter());
}

TNotNull<const UMetaHumanCharacterPipeline*> UMetaHumanItemEditorPipeline::GetRuntimeCharacterPipeline() const
{
	return GetRuntimePipeline();
}

#endif // WITH_EDITOR
