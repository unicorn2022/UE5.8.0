// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanItemPipeline.h"

#include "MetaHumanCharacterPalette.h"

#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"

#if WITH_EDITOR
UMetaHumanItemEditorPipeline* UMetaHumanItemPipeline::GetMutableEditorPipeline()
{
	return const_cast<UMetaHumanItemEditorPipeline*>(GetEditorPipeline());
}
#endif // WITH_EDITOR

void UMetaHumanItemPipeline::AssembleItemSynchronous(const FAssembleItemParams& Params,	FMetaHumanAssemblyOutput& OutAssemblyOutput) const
{
	FSharedEventRef Event;

	AssembleItem(Params,
		FOnAssemblyComplete::CreateLambda(
			[&OutAssemblyOutput, Event](FMetaHumanAssemblyOutput&& AssemblyOutput)
			{
				OutAssemblyOutput = MoveTemp(AssemblyOutput);
				Event->Trigger();
			}));

	Event->Wait();
}

void UMetaHumanItemPipeline::AssembleItem(
	const FMetaHumanPaletteItemPath& BaseItemPath,
	const TArray<FMetaHumanPipelineSlotSelectionData>& SlotSelections,
	const FMetaHumanPaletteBuiltData& ItemBuiltData,
	const FInstancedStruct& AssemblyInput,
	TNotNull<UObject*> OuterForGeneratedObjects,
	const FOnAssemblyComplete& OnComplete) const
{
	const FAssembleItemParams Params
	{
		.BaseItemPath = BaseItemPath,
		.OuterForGeneratedObjects = OuterForGeneratedObjects,
		.ItemBuiltData = ItemBuiltData.ItemBuiltData.View().FilterByBasePath(BaseItemPath),
		.SortedSlotSelections = MakeConstArrayView(SlotSelections),
		.AssemblyInput = AssemblyInput
	};

	AssembleItem(Params, OnComplete);
}

void UMetaHumanItemPipeline::AssembleItemSynchronous(
	const FMetaHumanPaletteItemPath& BaseItemPath,
	const TArray<FMetaHumanPipelineSlotSelectionData>& SlotSelections,
	const FMetaHumanPaletteBuiltData& ItemBuiltData,
	const FInstancedStruct& AssemblyInput,
	TNotNull<UObject*> OuterForGeneratedObjects,
	FMetaHumanAssemblyOutput& OutAssemblyOutput) const
{
	const FAssembleItemParams Params
	{
		.BaseItemPath = BaseItemPath,
		.OuterForGeneratedObjects = OuterForGeneratedObjects,
		.ItemBuiltData = ItemBuiltData.ItemBuiltData.View().FilterByBasePath(BaseItemPath),
		.SortedSlotSelections = MakeConstArrayView(SlotSelections),
		.AssemblyInput = AssemblyInput
	};

	AssembleItemSynchronous(Params, OutAssemblyOutput);
}

void UMetaHumanItemPipeline::SetPostAssemblyParameters(const FSetPostAssemblyParametersParams& Params, FInstancedStruct& InOutItemAssemblyOutput) const
{
	// TODO: Automatically route to sub-items, like UMetaHumanCollectionPipeline::SetPostAssemblyParameters does
}

bool UMetaHumanItemPipeline::AreSlotSelectionsAllowed(
	const FMetaHumanPaletteItemPath& BaseItemPath,
	TArrayView<const FMetaHumanPipelineSlotSelection> SlotSelections,
	const FMetaHumanPaletteBuiltData& ItemBuiltData,
	FText& OutDisallowedReason) const
{
	// By default all selections are allowed
	OutDisallowedReason = FText();
	return true;
}
