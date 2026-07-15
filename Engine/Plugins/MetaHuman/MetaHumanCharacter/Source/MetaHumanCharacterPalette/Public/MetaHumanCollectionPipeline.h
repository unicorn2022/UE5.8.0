// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCharacterPipeline.h"

#include "MetaHumanInstanceParameterCollection.h"
#include "MetaHumanPostAssemblyParameterOutputCollection.h"

#include "MetaHumanCollectionPipeline.generated.h"

#define UE_API METAHUMANCHARACTERPALETTE_API

class UMetaHumanCollection;
class UMetaHumanCollectionEditorPipeline;

/** A Collection-specific subclass of Character Pipeline */
UCLASS(Abstract, MinimalAPI)
class UMetaHumanCollectionPipeline : public UMetaHumanCharacterPipeline
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	/** Returns the editor-only component of this pipeline */
	virtual const UMetaHumanCollectionEditorPipeline* GetEditorPipeline() const
		PURE_VIRTUAL(UMetaHumanCollectionPipeline::GetEditorPipeline,return nullptr;);

	/** Override to narrow down the return type for collection pipelines */
	virtual UMetaHumanCollectionEditorPipeline* GetMutableEditorPipeline()
		PURE_VIRTUAL(UMetaHumanCollectionPipeline::GetMutableEditorPipeline, return nullptr;);
#endif

	/**
	 * Takes the opaque built data from the Collection and evaluates it with the given parameters
	 * to produce the meshes (etc) and populate the Assembly Output.
	 * 
	 * All entries in SlotSelections are guaranteed to reference valid items in the Collection,
	 * and AreSlotSelectionsAllowed must have returned true with this set of slot selections.
	 * 
	 * AssemblyParameters doesn't have to contain all parameters that were produced by the 
	 * build stage, it may just be a subset. Pipelines can read the full set of parameters from 
	 * the built data.
	 */
	struct FAssembleCollectionParams
	{
		TNotNull<const UMetaHumanCollection*> Collection;
		TNotNull<UObject*> OuterForGeneratedObjects;
		TConstArrayView<FMetaHumanPipelineSlotSelectionData> SlotSelections;
		FMetaHumanInstanceParameterCollectionView AssemblyParameters;
		FInstancedStruct AssemblyInput;
	};

	virtual void AssembleCollection(const FAssembleCollectionParams& Params, const FOnAssemblyComplete& OnComplete) const
		PURE_VIRTUAL(UMetaHumanCollectionPipeline::AssembleCollection,);

	UE_DEPRECATED(5.8, "This overload of AssembleCollection is deprecated")
	UE_API void AssembleCollection(
		TNotNull<const UMetaHumanCollection*> Collection,
		EMetaHumanCharacterPaletteBuildQuality Quality,
		const TArray<FMetaHumanPipelineSlotSelectionData>& SlotSelections,
		const FInstancedStruct& AssemblyInput,
		TNotNull<UObject*> OuterForGeneratedObjects,
		const FOnAssemblyComplete& OnComplete) const;

	/**
	 * Set Post-Assembly Parameters on the given item path. The empty item path signifies the 
	 * Collection's own parameters.
	 * 
	 * AllPostAssemblyParameters contains the complete set of Post-Assembly Parameters that were 
	 * produced during assembly and their parameter context. 
	 * 
	 * ModifiedPostAssemblyParameters contains any PAPs that have been modified since the last call
	 * to SetPostAssemblyParameters or the last assembly, whichever was more recent.
	 * 
	 * Note that PAPs can only be modified on one item at a time and this function must be called
	 * after each change. This shouldn't be too restrictive for most use cases and helps to 
	 * simplify Collection Pipeline implementations.
	 */
	UE_API virtual void SetPostAssemblyParameters(
		TNotNull<const UMetaHumanCollection*> Collection,
		const FMetaHumanPostAssemblyParameterOutputCollectionView& AllPostAssemblyParameters,
		const FMetaHumanPaletteItemPath& TargetItemPath,
		const FInstancedPropertyBag& ModifiedPostAssemblyParameters,
		FInstancedStruct& InOutCollectionAssemblyOutput) const;

	/**
	 * Returns an actor class that supports Instances targeting this pipeline.
	 *
	 * The returned class must implement IMetaHumanCharacterActorInterface.
	 *
	 * May return null.
	 */
	virtual TSubclassOf<AActor> GetActorClass() const
		PURE_VIRTUAL(UMetaHumanCollectionPipeline::GetActorClass, return nullptr;);

	/**
	 * Returns true if the given slot selections are a permitted combination.
	 * 
	 * Note that the Collection must have been built before calling this.
	 * 
	 * This allows the pipeline to restrict Instances to selecting items that are compatible with
	 * each other using any arbitary logic.
	 * 
	 * If AreSlotSelectionsAllowed returns false, the text assigned to OutDisallowedReason may be 
	 * shown to the user to explain why this combination is not allowed.
	 * 
	 * If SlotSelections is empty, AreSlotSelectionsAllowed must return true.
	 * 
	 * If there's no entry for a certain slot in the SlotSelections array, the pipeline must 
	 * implicitly make a valid selection for that slot. In other words, a pipeline can't return 
	 * false from AreSlotSelectionsAllowed for the reason that there's no explicit selection for a
	 * certain slot.
	 * 
	 * If SlotSelections specifies the empty item key for a slot, this means that no item is 
	 * selected. It's valid for a pipeline to *disallow* selecting no item for a slot, meaning that
	 * at least one item must be selected for the slot, but in that case, if there's no 
	 * SlotSelections entry for the slot it must implicitly select a valid item.
	 * 
	 * The order of elements in SlotSelections must not affect the return value of this function.
	 * It may affect OutDisallowedReason, for example if there are multiple reasons why the 
	 * selection is not allowed.
	 */
	UE_API virtual bool AreSlotSelectionsAllowed(
		TNotNull<const UMetaHumanCollection*> Collection,
		TArrayView<const FMetaHumanPipelineSlotSelection> SlotSelections,
		FText& OutDisallowedReason) const;

	/**
	 * Returns an item pipeline instance for given asset class.
	 * 
	 * This utility can be used to provide common item pipeline class for all
	 * principal assets of the given class, effectively removing the necessity
	 * for defining a pipeline for each wardrobe item asset.
	 */
	UE_API virtual const UMetaHumanItemPipeline* GetFallbackItemPipelineForAssetType(const TSoftClassPtr<UObject>& InAssetClass) const;

protected:
	/**
	 * Will be called when setting Post-Assembly Parameters if 
	 * EMetaHumanAssemblyOutputMappingMethod::CustomReference is specified for the target slot.
	 * 
	 * See comments on EMetaHumanAssemblyOutputMappingMethod.
	 */
	UE_API virtual FStructView GetItemAssemblyOutputReference(
		TNotNull<const UMetaHumanCollection*> Collection,
		const FMetaHumanPostAssemblyParameterOutputCollectionView& AllPostAssemblyParameters,
		const FMetaHumanPaletteItemPath& TargetItemPath,
		const FInstancedPropertyBag& ModifiedPostAssemblyParameters,
		FInstancedStruct& InCollectionAssemblyOutput) const;

	/**
	 * Will be called when setting Post-Assembly Parameters if 
	 * EMetaHumanAssemblyOutputMappingMethod::CustomValue is specified for the target slot.
	 * 
	 * See comments on EMetaHumanAssemblyOutputMappingMethod.
	 */
	UE_API virtual FInstancedStruct GetItemAssemblyOutputValue(
		TNotNull<const UMetaHumanCollection*> Collection,
		const FMetaHumanPostAssemblyParameterOutputCollectionView& AllPostAssemblyParameters,
		const FMetaHumanPaletteItemPath& TargetItemPath,
		const FInstancedPropertyBag& ModifiedPostAssemblyParameters,
		const FInstancedStruct& InCollectionAssemblyOutput) const;
	
	UE_API virtual void SetItemAssemblyOutputValue(
		TNotNull<const UMetaHumanCollection*> Collection,
		const FMetaHumanPostAssemblyParameterOutputCollectionView& AllPostAssemblyParameters,
		const FMetaHumanPaletteItemPath& TargetItemPath,
		const FInstancedPropertyBag& ModifiedPostAssemblyParameters,
		FInstancedStruct&& ItemAssemblyOutput,
		FInstancedStruct& InOutCollectionAssemblyOutput) const;
};

#undef UE_API
