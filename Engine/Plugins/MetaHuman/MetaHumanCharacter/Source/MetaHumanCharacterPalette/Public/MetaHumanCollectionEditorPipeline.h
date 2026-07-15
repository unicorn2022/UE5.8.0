// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCharacterEditorPipeline.h"
#include "MetaHumanTypes.h"

#include "MetaHumanCollectionEditorPipeline.generated.h"

struct FInstancedPropertyBag;
struct FMetaHumanCollectionBuiltData;
class UMetaHumanCollectionPipeline;
class UMetaHumanCollection;
class UMetaHumanInstance;
class UMetaHumanWardrobeItem;

#define UE_API METAHUMANCHARACTERPALETTE_API

/** 
 * The Build Input struct that will be set by the Character editor for builds initiated from there.
 * 
 * If your pipeline has a custom Build Input struct, have it inherit from this one for 
 * compatibility with the Character editor.
 */
USTRUCT(BlueprintType)
struct FMetaHumanBuildInputBase
{
	GENERATED_BODY()

	/**
	 * The Character being edited by this Character editor.
	 * 
	 * Pipelines should use the preview assets for this Character when building.
	 */
	UPROPERTY()
	FMetaHumanPaletteItemKey EditorPreviewCharacter;
};

/**
 * The editor-only component of a UMetaHumanCollectionPipeline.
 */
UCLASS(Abstract, MinimalAPI)
class UMetaHumanCollectionEditorPipeline : public UMetaHumanCharacterEditorPipeline
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	DECLARE_DELEGATE_TwoParams(FOnBuildComplete, EMetaHumanBuildStatus /* Status */, TSharedPtr<FMetaHumanCollectionBuiltData> /* BuiltData */);

	/**
	 * Build the Collection
	 * 
	 * Most code should call UMetaHumanCollection::Build instead of calling this directly.
	 * 
	 * For any slot that has at least one entry in SortedPinnedSlotSelections, the slot will be 
	 * locked to the given selections at assembly time and users won't be able to make any
	 * assignment to the slot on the Instance.
	 * 
	 * A null selection may be pinned by having a slot selection that specifies the null item.
	 * 
	 * Items assigned to a pinned slot that are not part of the pinned selection will be added to 
	 * SortedItemsToExclude automatically.
	 * 
	 * SortedPinnedSlotSelections is a hint to the pipeline that no other selection will be 
	 * possible for the slot. The pipeline is not obliged to use this information for anything, but
	 * it may choose to bake certain data into its build output based on its knowledge about which
	 * items will be selected at assembly time. 
	 * 
	 * Items named in pinned selections should still produce build output, and will still have 
	 * AssembleItem called for them when the Instance is assembled. Pinned items may still produce 
	 * Instance Parameters during assembly.
	 */
	struct FBuildCollectionParams
	{
		TNotNull<const UMetaHumanCollection*> Collection;
		TNotNull<UObject*> OuterForGeneratedAssets;
		TConstArrayView<FMetaHumanPinnedSlotSelection> SortedPinnedSlotSelections;
		TConstArrayView<FMetaHumanPaletteItemPath> SortedItemsToExclude;
		FInstancedStruct BuildInput;
	};

	virtual void BuildCollection(const FBuildCollectionParams& Params, const FOnBuildComplete& OnComplete) const
		PURE_VIRTUAL(UMetaHumanCollectionEditorPipeline::BuildCollection,);

	/**
	 * Returns true if the Collection is in a valid state to be built.
	 * 
	 * Implementations may modify this pipeline instance or the Collection to automatically fix
	 * configuration errors. They may also show interactive UI when FApp::IsUnattended() returns 
	 * false.
	 *
	 * If this function returns false, the build will be aborted.
	 */
	UE_API virtual bool ValidateCollection(TNotNull<UMetaHumanCollection*> Collection);

	UE_DEPRECATED(5.8, "Quality is now a property of the Collection. TargetPlatform is no longer used at build time. Use the new BuildCollection overload.")
	UE_API void BuildCollection(
		TNotNull<const UMetaHumanCollection*> Collection,
		TNotNull<UObject*> OuterForGeneratedAssets,
		const TArray<FMetaHumanPinnedSlotSelection>& SortedPinnedSlotSelections,
		const TArray<FMetaHumanPaletteItemPath>& SortedItemsToExclude,
		const FInstancedStruct& BuildInput,
		EMetaHumanCharacterPaletteBuildQuality Quality,
		ITargetPlatform* TargetPlatform,
		const FOnBuildComplete& OnComplete) const;

	/**
	 * Utility to check if the pipeline has valid properties to build and unpack a collection
	 * Gives the opportunity for backwards compatibility checks and user prompts before even attempting to unpack
	 */
	virtual bool CanBuild() const
		PURE_VIRTUAL(UMetaHumanCollectionEditorPipeline::CanBuild, return true;);

	/**
	 * Returns true if the given MetaHuman Instance should have its assembly output baked into
	 * the cooked package by default.
	 */
	UE_API virtual bool ShouldCookInstanceAsAssembled(TNotNull<const UMetaHumanInstance*> Instance) const;

	/**
	 * IMPORTANT: Don't call this directly. Call UnpackAssets on the Collection.
	 * 
	 * Moves any internal assets from the Collection's built data, such as meshes and textures, out 
	 * to their own packages, so that they are visible in the Content Browser.
	 * 
	 * These assets will still be referenced from the Collection's built data.
	 * 
	 * Requires the Collection to be built first.
	 */
	UE_API virtual void UnpackCollectionAssets(
		TNotNull<const UMetaHumanCollection*> Collection,
		FMetaHumanCollectionBuiltData& CollectionBuiltData,
		const FOnUnpackComplete& OnComplete) const;

	/**
	 * Creates a FMetaHumanCharacterPaletteItem for a Wardrobe Item whose pipeline is not 
	 * directly compatible with the target slot.
	 *
	 * The implementation should:
	 *  - Create a new internal UMetaHumanWardrobeItem as a subobject of the Collection
	 *  - Set up the appropriate compatible item pipeline on it
	 *  - Populate the new pipeline, e.g. copy relevant settings from the source Wardrobe Item
	 *
	 * The Variation field does not need to be set; the Collection will ensure uniqueness.
	 *
	 * @param Collection          The Collection being imported into (use as outer for the
	 *                            new internal WI)
	 * @param SlotName            The target slot
	 * @param SourceWardrobeItem  The WI of the item being imported
	 * @param OutItem             The item to be added to the Collection
	 * @return true if the item was created successfully
	 */
	UE_API virtual bool TryCreateItemForImport(
		TNotNull<UMetaHumanCollection*> Collection,
		FName SlotName,
		TNotNull<const UMetaHumanWardrobeItem*> SourceWardrobeItem,
		FMetaHumanCharacterPaletteItem& OutItem);

	/**
	 * IMPORTANT: Don't call this directly. Call TryUnpack on the Instance.
	 * 
	 * Unpacks any assets generated during Assembly and contained in the Instance itself.
	 */
	virtual bool TryUnpackInstanceAssets(
		TNotNull<UMetaHumanInstance*> Instance,
		FInstancedStruct& AssemblyOutput,
		TArray<FMetaHumanGeneratedAssetMetadata>& AssemblyAssetMetadata,
		const FString& TargetFolder) const
		PURE_VIRTUAL(UMetaHumanCollectionEditorPipeline::TryUnpackInstanceAssets,return false;);

	/** Returns the runtime pipeline instance corresponding to this editor pipeline instance. */
	UE_API virtual TNotNull<const UMetaHumanCollectionPipeline*> GetRuntimePipeline() const;

	/** Calls GetRuntimePipeline. No need for subclasses to implement this. */
	UE_API virtual TNotNull<const UMetaHumanCharacterPipeline*> GetRuntimeCharacterPipeline() const override;

	/**
	 * Returns an actor class that supports Instances targeting this pipeline.
	 * 
	 * This actor type will be used in the Character editor viewport.
	 *
	 * The returned class must implement IMetaHumanCharacterEditorActorInterface.
	 *
	 * If this returns null, it will be treated as an error but callers will handle it gracefully.
	 * Pipelines that don't have their own editor actor class can return AMetaHumanCharacterEditorActor::StaticClass().
	 */
	virtual TSubclassOf<AActor> GetEditorActorClass() const
		PURE_VIRTUAL(UMetaHumanCollectionEditorPipeline::GetEditorActorClass, return nullptr;);

	/**
	 * NOTE
	 * 
	 * The following functions are only used by the MetaHuman Character editor when it writes assets to disk.
	 * 
	 * In a future release, they may be moved to a different class.
	 */

	/**
	 * These functions are called before and after the Character editor assembly process.
	 * 
	 * The passed-in Collection is a copy of the Character's internal Collection, and this pipeline
	 * instance is also a copy, so these functions may modify the Collection and pipeline without 
	 * affecting the associated Character.
	 */
	UE_API virtual bool BeginCharacterEditorAssemble(TNotNull<UMetaHumanCollection*> InCollection, const FString& InCharacterName);
	UE_API virtual void EndCharacterEditorAssemble(TNotNull<UMetaHumanCollection*> InCollection);

	/**
	 * Generates a blueprint actor asset on the given path and quality level.
	 * 
	 * Resulting blueprint should be an asset based on or a duplicate of the GetActorClass(), but this is
	 * implementation dependent.
	 */
	UE_DEPRECATED(5.7, "Use WriteActorBlueprint(const FWriteBlueprintSettings&) instead.")
	UE_API virtual UBlueprint* WriteActorBlueprint(const FString & InBlueprintPath) const;

	struct FWriteBlueprintSettings
	{
		FString BlueprintPath;
		EMetaHumanQualityLevel QualityLevel = EMetaHumanQualityLevel::Cinematic;
		FName AnimationSystemName;
	};
	UE_API virtual UBlueprint* WriteActorBlueprint(const FWriteBlueprintSettings& InWriteBlueprintSettings) const;
	
	/**
	 * Updates the given blueprint asset with the given instance.
	 * 
	 * In the implementation, user can add components to the blueprint or reconfigure
	 * it depending on the parameters (e.g. legacy, export quality etc.).
	 */
	UE_API virtual bool UpdateActorBlueprint(
		const UMetaHumanInstance* InCharacterInstance,
		UBlueprint* InBlueprint) const;
#endif // WITH_EDITOR
};

#undef UE_API

