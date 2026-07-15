// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanPaletteItemKey.h"
#include "MetaHumanPaletteItemPath.h"

#include "MetaHumanPipelineSlotSelection.generated.h"

/** An item selected for a slot */
USTRUCT(BlueprintType, meta = (HasNativeMake="/Script/MetaHumanCharacterPalette.MetaHumanPipelineSlotSelectionBlueprintLibrary.MakeSlotSelection"))
struct FMetaHumanPipelineSlotSelection
{
	GENERATED_BODY()

public:
	FMetaHumanPipelineSlotSelection() = default;
	FMetaHumanPipelineSlotSelection(const FMetaHumanPipelineSlotSelection&) = default;
	FMetaHumanPipelineSlotSelection(FMetaHumanPipelineSlotSelection&&) = default;
	FMetaHumanPipelineSlotSelection& operator=(const FMetaHumanPipelineSlotSelection&) = default;
	FMetaHumanPipelineSlotSelection& operator=(FMetaHumanPipelineSlotSelection&&) = default;
	~FMetaHumanPipelineSlotSelection() = default;

	METAHUMANCHARACTERPALETTE_API FMetaHumanPipelineSlotSelection(FName InSlotName, const FMetaHumanPaletteItemKey& InSelectedItem);
	METAHUMANCHARACTERPALETTE_API FMetaHumanPipelineSlotSelection(const FMetaHumanPaletteItemPath& InParentItemPath, FName InSlotName, const FMetaHumanPaletteItemKey& InSelectedItem);

	// Allow the compiler to generate default equality operations
	METAHUMANCHARACTERPALETTE_API friend bool operator==(const FMetaHumanPipelineSlotSelection&, const FMetaHumanPipelineSlotSelection&) = default;
	METAHUMANCHARACTERPALETTE_API friend bool operator!=(const FMetaHumanPipelineSlotSelection&, const FMetaHumanPipelineSlotSelection&) = default;

	/** 
	 * Define less-than for sorting by item path.
	 * 
	 * Only if the full item path is equal will SlotName be compared.
	 * 
	 * This operator is designed for fast sorting at runtime and may not give the same results in
	 * different instances of the engine.
	 */
	METAHUMANCHARACTERPALETTE_API friend bool operator<(const FMetaHumanPipelineSlotSelection& A, const FMetaHumanPipelineSlotSelection& B);

	/** Returns the full path to the selected item */
	METAHUMANCHARACTERPALETTE_API FMetaHumanPaletteItemPath GetSelectedItemPath() const;

	/**
	 * The path to the Collection or Wardrobe Item that contains the slot referenced by SlotName.
	 * 
	 * If the slot is on the Collection itself, this path will be empty.
	 */
	UPROPERTY(EditAnywhere, Category = "Pipeline")
	FMetaHumanPaletteItemPath ParentItemPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pipeline")
	FName SlotName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pipeline")
	FMetaHumanPaletteItemKey SelectedItem;
};
