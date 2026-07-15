// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCollectionPipeline.h"

#include "MetaHumanCollection.h"
#include "MetaHumanCharacterPaletteLog.h"
#include "MetaHumanCharacterPipelineSpecification.h"
#include "MetaHumanItemPipeline.h"

#include "Logging/StructuredLog.h"

void UMetaHumanCollectionPipeline::AssembleCollection(
	TNotNull<const UMetaHumanCollection*> Collection,
	EMetaHumanCharacterPaletteBuildQuality Quality,
	const TArray<FMetaHumanPipelineSlotSelectionData>& SlotSelections,
	const FInstancedStruct& AssemblyInput,
	TNotNull<UObject*> OuterForGeneratedObjects,
	const FOnAssemblyComplete& OnComplete) const
{
	const FAssembleCollectionParams Params
	{
		.Collection = Collection,
		.OuterForGeneratedObjects = OuterForGeneratedObjects,
		.SlotSelections = MakeConstArrayView(SlotSelections),
		.AssemblyInput = AssemblyInput
	};

	AssembleCollection(Params, OnComplete);
}

void UMetaHumanCollectionPipeline::SetPostAssemblyParameters(
	TNotNull<const UMetaHumanCollection*> Collection,
	const FMetaHumanPostAssemblyParameterOutputCollectionView& AllPostAssemblyParameters,
	const FMetaHumanPaletteItemPath& TargetItemPath,
	const FInstancedPropertyBag& ModifiedPostAssemblyParameters,
	FInstancedStruct& InOutCollectionAssemblyOutput) const
{
	// These are guaranteed by the framework
	check(Collection->GetBuiltData().IsValid());
	check(InOutCollectionAssemblyOutput.IsValid());
	
	// If TargetItemPath is empty, the caller is trying to set parameters on the Collection 
	// Pipeline. This needs to be handled in the pipeline's implementation of this function.
	if (TargetItemPath.IsEmpty())
	{
		return;
	}

	const FMetaHumanPaletteItemKey BaseItemKey = TargetItemPath.GetPathEntry(0);
	const FMetaHumanPaletteItemPath BaseItemPath(BaseItemKey);

	FMetaHumanCharacterPaletteItem Item;
	// The given item path is guaranteed by the framework to reference a valid item
	verify(Collection->TryFindItem(BaseItemKey, Item));

	const UMetaHumanItemPipeline* ItemPipeline = nullptr;
	if (!Collection->TryResolveItemPipeline(BaseItemPath, ItemPipeline))
	{
		// Some items may legitimately not have an item pipeline
		return;
	}

	TNotNull<const UMetaHumanCharacterPipelineSpecification*> PipelineSpec = GetSpecification();
	const TOptional<FName> RealSlotName = PipelineSpec->ResolveRealSlotName(Item.SlotName);
	if (!RealSlotName.IsSet())
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, 
			"SetPostAssemblyParameters: Failed to resolve slot {Slot} on pipeline {Pipeline}",
			Item.SlotName.ToString(), GetPathName());

		return;
	}

	// If ResolveRealSlotName returned this slot name, it must be valid
	const FMetaHumanCharacterPipelineSlot& SlotSpec = PipelineSpec->Slots[RealSlotName.GetValue()];

	FInstancedStruct ItemAssemblyOutput;

	if (SlotSpec.AssemblyOutputMapping.Method == EMetaHumanAssemblyOutputMappingMethod::DirectProperty)
	{
		const FStructProperty* PipelineProperty = CastField<FStructProperty>(
			InOutCollectionAssemblyOutput.GetScriptStruct()->FindPropertyByName(SlotSpec.AssemblyOutputMapping.PipelineOutputPropertyName));

		if (!PipelineProperty)
		{
			UE_LOGFMT(LogMetaHumanCharacterPalette, Error, 
				"SetPostAssemblyParameters: Couldn't find struct property {Property} on assembly output struct {Struct}",
				SlotSpec.AssemblyOutputMapping.PipelineOutputPropertyName.ToString(), InOutCollectionAssemblyOutput.GetScriptStruct()->GetPathName());

			return;
		}

		check(PipelineProperty->Struct);

		uint8* PipelinePropertyValuePtr = PipelineProperty->ContainerPtrToValuePtr<uint8>(InOutCollectionAssemblyOutput.GetMutableMemory());

		if (SlotSpec.AssemblyOutputMapping.PipelineOutputInnerPropertyName != NAME_None)
		{
			// PipelineProperty points to a struct on the assembly output, and the property we're 
			// looking for is a property on this struct.
			//
			// In other words, because PipelineOutputInnerPropertyName is set, the item's assembly
			// output is stored as CollectionAssemblyOutput.IntermediateStruct.ItemAssemblyOutput.
			// If PipelineOutputInnerPropertyName is unset, it's CollectionAssemblyOutput.ItemAssemblyOutput.

			const FStructProperty* InnerPipelineProperty = CastField<FStructProperty>(
				PipelineProperty->Struct->FindPropertyByName(SlotSpec.AssemblyOutputMapping.PipelineOutputInnerPropertyName));

			if (!InnerPipelineProperty)
			{
				UE_LOGFMT(LogMetaHumanCharacterPalette, Error, 
					"SetPostAssemblyParameters: Couldn't find struct property {Property} on inner struct {Struct}",
					SlotSpec.AssemblyOutputMapping.PipelineOutputInnerPropertyName.ToString(), PipelineProperty->Struct->GetPathName());

				return;
			}

			PipelineProperty = InnerPipelineProperty;
			PipelinePropertyValuePtr = PipelineProperty->ContainerPtrToValuePtr<uint8>(PipelinePropertyValuePtr);
		}

		if (SlotSpec.AssemblyOutputStruct != PipelineProperty->Struct)
		{
			UE_LOGFMT(LogMetaHumanCharacterPalette, Error, 
				"SetPostAssemblyParameters: Struct found by DirectProperty mapping for slot {Slot} is not of the expected type. Expected {SlotSpecStruct}, found {PropertyStruct}",
				RealSlotName.GetValue().ToString(), SlotSpec.AssemblyOutputStruct->GetPathName(), PipelineProperty->Struct->GetPathName());

			return;
		}

		const FStructView PropertyView(PipelineProperty->Struct, PipelinePropertyValuePtr);
		ItemAssemblyOutput = FInstancedStruct(PropertyView);
	}
	else if (SlotSpec.AssemblyOutputMapping.Method == EMetaHumanAssemblyOutputMappingMethod::ArrayProperty)
	{
		if (!AllPostAssemblyParameters.Contains(BaseItemPath))
		{
			// Whether the base item has parameters of its own or not, it needs to set PipelineAssemblyOutputArrayIndex

			UE_LOGFMT(LogMetaHumanCharacterPalette, Error, 
				"SetPostAssemblyParameters: PostAssemblyParameters not populated for item {Item} using ArrayProperty mapping",
				BaseItemPath.ToDebugString());
			
			return;
		}

		const FMetaHumanPostAssemblyParameterOutput& PAPOutput = AllPostAssemblyParameters[BaseItemPath];
		if (PAPOutput.PipelineAssemblyOutputArrayIndex == INDEX_NONE)
		{
			// No array index was assigned for this item, so the item assembly output can't be 
			// found on the pipeline assembly output.

			UE_LOGFMT(LogMetaHumanCharacterPalette, Error, 
				"SetPostAssemblyParameters: PipelineAssemblyOutputArrayIndex not set for item {Item} using ArrayProperty mapping",
				BaseItemPath.ToDebugString());

			return;
		}

		const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(
			InOutCollectionAssemblyOutput.GetScriptStruct()->FindPropertyByName(SlotSpec.AssemblyOutputMapping.PipelineOutputPropertyName));

		if (!ArrayProperty)
		{
			UE_LOGFMT(LogMetaHumanCharacterPalette, Error, 
				"SetPostAssemblyParameters: Couldn't find array property {Property} on assembly output struct {Struct}",
				SlotSpec.AssemblyOutputMapping.PipelineOutputPropertyName.ToString(), InOutCollectionAssemblyOutput.GetScriptStruct()->GetPathName());

			return;
		}

		const FStructProperty* PipelineProperty = CastField<FStructProperty>(ArrayProperty->Inner);
		if (!PipelineProperty)
		{
			UE_LOGFMT(LogMetaHumanCharacterPalette, Error, 
				"SetPostAssemblyParameters: Element type of array property {Property} is not a struct",
				SlotSpec.AssemblyOutputMapping.PipelineOutputPropertyName.ToString());

			return;
		}

		check(PipelineProperty->Struct);

		// A pointer to the TArray containing the item assembly output
		const uint8* ArrayValuePtr = ArrayProperty->ContainerPtrToValuePtr<uint8>(InOutCollectionAssemblyOutput.GetMemory());
		FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayValuePtr);

		if (PAPOutput.PipelineAssemblyOutputArrayIndex >= ArrayHelper.Num())
		{
			UE_LOGFMT(LogMetaHumanCharacterPalette, Error, 
				"SetPostAssemblyParameters: PipelineAssemblyOutputArrayIndex out of bounds for item {Item}",
				BaseItemPath.ToDebugString());

			return;
		}

		uint8* PipelinePropertyValuePtr = ArrayHelper.GetElementPtr(PAPOutput.PipelineAssemblyOutputArrayIndex);

		if (SlotSpec.AssemblyOutputMapping.PipelineOutputInnerPropertyName != NAME_None)
		{
			// PipelineProperty points to a struct in an array on the assembly output, and the 
			// property we're looking for is a property on this struct.
			//
			// In other words, because PipelineOutputInnerPropertyName is set, the item's assembly
			// output is stored as CollectionAssemblyOutput.IntermediateArray[PipelineAssemblyOutputArrayIndex].ItemAssemblyOutput.
			// If PipelineOutputInnerPropertyName is unset, it's CollectionAssemblyOutput.ItemAssemblyOutputArray[PipelineAssemblyOutputArrayIndex].

			const FStructProperty* InnerPipelineProperty = CastField<FStructProperty>(
				PipelineProperty->Struct->FindPropertyByName(SlotSpec.AssemblyOutputMapping.PipelineOutputInnerPropertyName));

			if (!InnerPipelineProperty)
			{
				UE_LOGFMT(LogMetaHumanCharacterPalette, Error, 
					"SetPostAssemblyParameters: Couldn't find struct property {Property} on inner struct {Struct}",
					SlotSpec.AssemblyOutputMapping.PipelineOutputInnerPropertyName.ToString(), PipelineProperty->Struct->GetPathName());

				return;
			}

			PipelineProperty = InnerPipelineProperty;
			PipelinePropertyValuePtr = PipelineProperty->ContainerPtrToValuePtr<uint8>(PipelinePropertyValuePtr);
		}

		if (SlotSpec.AssemblyOutputStruct != PipelineProperty->Struct)
		{
			UE_LOGFMT(LogMetaHumanCharacterPalette, Error, 
				"SetPostAssemblyParameters: Struct found by ArrayProperty mapping for slot {Slot} is not of the expected type. Expected {SlotSpecStruct}, found {PropertyStruct}",
				RealSlotName.GetValue().ToString(), SlotSpec.AssemblyOutputStruct->GetPathName(), PipelineProperty->Struct->GetPathName());

			return;
		}

		const FStructView PropertyView(PipelineProperty->Struct, PipelinePropertyValuePtr);
		ItemAssemblyOutput = FInstancedStruct(PropertyView);
	}
	else if (SlotSpec.AssemblyOutputMapping.Method == EMetaHumanAssemblyOutputMappingMethod::CustomReference)
	{
		const FStructView ItemAssemblyOutputView = 
			GetItemAssemblyOutputReference(
				Collection, 
				AllPostAssemblyParameters, 
				TargetItemPath, 
				ModifiedPostAssemblyParameters, 
				InOutCollectionAssemblyOutput);

		if (!ItemAssemblyOutputView.IsValid())
		{
			UE_LOGFMT(LogMetaHumanCharacterPalette, Error, 
				"SetPostAssemblyParameters: No struct reference returned from GetItemAssemblyOutputReference for item {Item}",
				TargetItemPath.ToDebugString());

			return;
		}

		ItemAssemblyOutput = FInstancedStruct(ItemAssemblyOutputView);
	}
	else if (SlotSpec.AssemblyOutputMapping.Method == EMetaHumanAssemblyOutputMappingMethod::CustomValue)
	{
		ItemAssemblyOutput = 
			GetItemAssemblyOutputValue(
				Collection, 
				AllPostAssemblyParameters, 
				TargetItemPath, 
				ModifiedPostAssemblyParameters, 
				InOutCollectionAssemblyOutput);

		if (!ItemAssemblyOutput.IsValid())
		{
			UE_LOGFMT(LogMetaHumanCharacterPalette, Error, 
				"SetPostAssemblyParameters: No struct value returned from GetItemAssemblyOutputValue for item {Item}",
				TargetItemPath.ToDebugString());

			return;
		}
	}	
	else if (SlotSpec.AssemblyOutputMapping.Method == EMetaHumanAssemblyOutputMappingMethod::Ignore)
	{
		// The spec says we should not attempt to handle this slot
		return;
	}
	else
	{
		// A new method has been added to the enum but not implemented here
		checkNoEntry();
		return;
	}

	// Code above should either set ItemAssemblyOutput to a valid struct or early out
	check(ItemAssemblyOutput.IsValid());

	const UMetaHumanItemPipeline::FSetPostAssemblyParametersParams ItemParams
	{
		.Quality = Collection->GetQuality(),
		.BaseItemPath = BaseItemPath,
		.ItemBuiltData = Collection->GetBuiltData().PaletteBuiltData.ItemBuiltData.View().FilterByBasePath(BaseItemPath),
		.AllPostAssemblyParameters = AllPostAssemblyParameters.FilterByBasePath(BaseItemPath),
		.TargetItemPath = TargetItemPath,
		.ModifiedPostAssemblyParameters = ModifiedPostAssemblyParameters
	};

	ItemPipeline->SetPostAssemblyParameters(ItemParams, ItemAssemblyOutput);

	if (SlotSpec.AssemblyOutputMapping.Method == EMetaHumanAssemblyOutputMappingMethod::CustomValue)
	{
		if (!ItemAssemblyOutput.IsValid())
		{
			UE_LOGFMT(LogMetaHumanCharacterPalette, Error, 
				"SetPostAssemblyParameters: ItemAssemblyOutput has been cleared by the item pipeline for item {Item}",
				TargetItemPath.ToDebugString());

			return;
		}

		SetItemAssemblyOutputValue(
			Collection, 
			AllPostAssemblyParameters, 
			TargetItemPath, 
			ModifiedPostAssemblyParameters, 
			MoveTemp(ItemAssemblyOutput),
			InOutCollectionAssemblyOutput);
	}
}

FStructView UMetaHumanCollectionPipeline::GetItemAssemblyOutputReference(
	TNotNull<const UMetaHumanCollection*> Collection,
	const FMetaHumanPostAssemblyParameterOutputCollectionView& AllPostAssemblyParameters,
	const FMetaHumanPaletteItemPath& TargetItemPath,
	const FInstancedPropertyBag& ModifiedPostAssemblyParameters,
	FInstancedStruct& InCollectionAssemblyOutput) const
{
	UE_LOGFMT(LogMetaHumanCharacterPalette, Error, 
		"Base implementation of GetItemAssemblyOutputReference called for item {ItemPath}. A slot specification is set to the CustomReference mapping method, but there's no implementation to do the mapping.",
		TargetItemPath.ToDebugString());

	return FStructView();
}

FInstancedStruct UMetaHumanCollectionPipeline::GetItemAssemblyOutputValue(
	TNotNull<const UMetaHumanCollection*> Collection,
	const FMetaHumanPostAssemblyParameterOutputCollectionView& AllPostAssemblyParameters,
	const FMetaHumanPaletteItemPath& TargetItemPath,
	const FInstancedPropertyBag& ModifiedPostAssemblyParameters,
	const FInstancedStruct& InCollectionAssemblyOutput) const
{
	UE_LOGFMT(LogMetaHumanCharacterPalette, Error, 
		"Base implementation of GetItemAssemblyOutputValue called for item {ItemPath}. A slot specification is set to the CustomValue mapping method, but there's no implementation to do the mapping.",
		TargetItemPath.ToDebugString());

	return FInstancedStruct();
}
	
void UMetaHumanCollectionPipeline::SetItemAssemblyOutputValue(
	TNotNull<const UMetaHumanCollection*> Collection,
	const FMetaHumanPostAssemblyParameterOutputCollectionView& AllPostAssemblyParameters,
	const FMetaHumanPaletteItemPath& TargetItemPath,
	const FInstancedPropertyBag& ModifiedPostAssemblyParameters,
	FInstancedStruct&& ItemAssemblyOutput,
	FInstancedStruct& InOutCollectionAssemblyOutput) const
{
	UE_LOGFMT(LogMetaHumanCharacterPalette, Error, 
		"Base implementation of SetItemAssemblyOutputValue called for item {ItemPath}. A slot specification is set to the CustomValue mapping method, but there's no implementation to do the mapping.",
		TargetItemPath.ToDebugString());
}

bool UMetaHumanCollectionPipeline::AreSlotSelectionsAllowed(
	TNotNull<const UMetaHumanCollection*> Collection,
	TArrayView<const FMetaHumanPipelineSlotSelection> SlotSelections,
	FText& OutDisallowedReason) const
{
	// TODO: This should call AreSlotSelectionsAllowed on item pipelines

	// By default all selections are allowed
	OutDisallowedReason = FText();
	return true;
}

const UMetaHumanItemPipeline* UMetaHumanCollectionPipeline::GetFallbackItemPipelineForAssetType(const TSoftClassPtr<UObject>& InAssetClass) const
{
	return nullptr;
}
