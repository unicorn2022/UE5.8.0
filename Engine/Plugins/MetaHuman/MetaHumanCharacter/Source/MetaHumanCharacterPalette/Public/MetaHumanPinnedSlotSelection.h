// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanPipelineSlotSelection.h"

#include "StructUtils/PropertyBag.h"

#include "MetaHumanPinnedSlotSelection.generated.h"

/** 
 * An item pinned to a slot at build time
 * 
 * At assembly time, if a slot has any pinned items, it won't be able to have non-pinned items 
 * selected for it.
 */
USTRUCT()
struct FMetaHumanPinnedSlotSelection
{
	GENERATED_BODY()

public:
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FMetaHumanPinnedSlotSelection() = default;
	FMetaHumanPinnedSlotSelection(const FMetaHumanPinnedSlotSelection& Other) = default;
	FMetaHumanPinnedSlotSelection(FMetaHumanPinnedSlotSelection&& Other) = default;
	FMetaHumanPinnedSlotSelection& operator=(const FMetaHumanPinnedSlotSelection& Other) = default;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	METAHUMANCHARACTERPALETTE_API static bool IsItemPinned(TConstArrayView<FMetaHumanPinnedSlotSelection> SortedSelections, const FMetaHumanPaletteItemPath& ItemPath);
	METAHUMANCHARACTERPALETTE_API static bool TryGetPinnedItem(TConstArrayView<FMetaHumanPinnedSlotSelection> SortedSelections, const FMetaHumanPaletteItemPath& ItemPath, const FMetaHumanPinnedSlotSelection*& OutPinnedItem);

	UPROPERTY(EditAnywhere, Category = "Pipeline")
	FMetaHumanPipelineSlotSelection Selection;

	UE_DEPRECATED(5.8, "There are now two types of Instance Parameters: Assembly Parameters and Post-Assembly Parameters. Only Assembly Parameters are used in this context.")
	UPROPERTY(EditAnywhere, Category = "Pipeline")
	FInstancedPropertyBag InstanceParameters;

	/**
	 * If the pipeline does any baking at build time that would use Assembly Parameters, it should
	 * use these values.
	 * 
	 * If the parameters are still settable, they will be passed in again after assembly, so 
	 * pipelines don't have to store this data at build time if they don't do anything with it.
	 */
	UPROPERTY(EditAnywhere, Category = "Pipeline")
	FInstancedPropertyBag AssemblyParameters;
};
