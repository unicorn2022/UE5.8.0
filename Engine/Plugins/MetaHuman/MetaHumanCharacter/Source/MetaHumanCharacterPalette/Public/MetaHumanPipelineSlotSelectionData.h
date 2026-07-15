// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanPipelineSlotSelection.h"

#include "MetaHumanPipelineSlotSelectionData.generated.h"

struct FMetaHumanPipelineSlotSelection;
class UMetaHumanItemPipeline;

/** 
 * An item selected for a slot, with additional data about the selection.
 * 
 * It's a fine distinction, but FMetaHumanPipelineSlotSelection is like a key that identifies the 
 * selection, which may be passed around via public APIs. This struct contains data that is used to
 * process the selection.
 */
USTRUCT(BlueprintType)
struct FMetaHumanPipelineSlotSelectionData
{
	GENERATED_BODY()

public:
	FMetaHumanPipelineSlotSelectionData() = default;
	~FMetaHumanPipelineSlotSelectionData() = default;
	FMetaHumanPipelineSlotSelectionData(const FMetaHumanPipelineSlotSelectionData&) = default;
	METAHUMANCHARACTERPALETTE_API explicit FMetaHumanPipelineSlotSelectionData(const FMetaHumanPipelineSlotSelection& InSelection);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slot")
	FMetaHumanPipelineSlotSelection Selection;

	// No additional data about the selection is stored here yet. This type is kept to allow future expansion.
};
