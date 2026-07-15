// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCharacterPalette.h"

#include "MetaHumanCharacterEditorPipeline.h"
#include "MetaHumanPinnedSlotSelection.h"

#include "MetaHumanCollection.generated.h"

class UMetaHumanCollectionPipeline;
class UMetaHumanCollectionEditorPipeline;

#if WITH_EDITORONLY_DATA
UENUM()
enum class EMetaHumanCharacterUnpackPathMode : uint8
{
	/**
	 * Assets will be unpacked to a subfolder of the Palette's current folder.
	 * The subfolder will have the same name as the Palette.
	 *
	 * For example, when a Palette called A is at path /Game/Palettes, its assets
	 * would be unpacked to the /Game/Palettes/A folder.
	 */
	SubfolderNamedForPalette UMETA(Hidden),

	/** UnpackFolderPath is a relative path from the folder containing the Palette */
	Relative,

	/** UnpackFolderPath is an absolute path */
	Absolute
};

UENUM()
enum class EMetaHumanCharacterAssetsUnpackResult : uint8
{
	Succeeded,
	Failed
};

DECLARE_DELEGATE_OneParam(FOnMetaHumanCharacterAssetsUnpacked, EMetaHumanCharacterAssetsUnpackResult /* Result */);
#endif

/** The output of the Collection Pipeline's build step */
USTRUCT()
struct FMetaHumanCollectionBuiltData
{
	GENERATED_BODY()

public:
	/** Returns true if this built data has been populated from a successful build */
	METAHUMANCHARACTERPALETTE_API bool IsValid() const;

	UPROPERTY()
	FMetaHumanPaletteBuiltData PaletteBuiltData;

	UPROPERTY()
	TArray<FMetaHumanPinnedSlotSelection> SortedPinnedSlotSelections;
};

/**
 * A collection of character parts (e.g. MetaHuman Characters, clothing, hairstyles) that target 
 * slots on a Character Pipeline.
 * 
 * Create an Instance from a Collection to assemble a renderable character from the parts
 * contained in the Collection.
 */
UCLASS(BlueprintType)
class METAHUMANCHARACTERPALETTE_API UMetaHumanCollection : public UMetaHumanCharacterPalette
{
	GENERATED_BODY()

public:
	UE_DEPRECATED(5.8, "FOnPaletteBuilt has been replaced by FOnCollectionBuilt")
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPaletteBuilt, EMetaHumanCharacterPaletteBuildQuality /* Quality */);
	
	UMetaHumanCollection();

	// Collections should not be created or modified outside the editor
#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE(FOnCollectionBuilt);
	DECLARE_MULTICAST_DELEGATE(FOnPipelineChanged);
	DECLARE_DELEGATE_OneParam(FOnBuildComplete, EMetaHumanBuildStatus /* Status */);

	/** 
	 * Set the Quality that will be used when this Collection is built.
	 * 
	 * Clears built data if changing to a different quality level.
	 */
	void SetQuality(EMetaHumanCharacterPaletteBuildQuality InQuality);
	
	/** Builds the collection so that Instances can assemble characters from it */
	void Build(
		const FInstancedStruct& BuildInput,
		const FOnBuildComplete& OnComplete,
		const TArray<FMetaHumanPinnedSlotSelection>& PinnedSlotSelections = TArray<FMetaHumanPinnedSlotSelection>(),
		const TArray<FMetaHumanPaletteItemPath>& ItemsToExclude = TArray<FMetaHumanPaletteItemPath>());

	UE_DEPRECATED(5.8, "Quality is now a property of the Collection. TargetPlatform is no longer used at build time. Use the new Build overload.")
	void Build(
		const FInstancedStruct& BuildInput,
		EMetaHumanCharacterPaletteBuildQuality InQuality,
		ITargetPlatform* TargetPlatform,
		const FOnBuildComplete& OnComplete,
		const TArray<FMetaHumanPinnedSlotSelection>& PinnedSlotSelections = TArray<FMetaHumanPinnedSlotSelection>(),
		const TArray<FMetaHumanPaletteItemPath>& ItemsToExclude = TArray<FMetaHumanPaletteItemPath>());

	/** Clear any built data in this Collection, resetting it to an unbuilt state */
	void ClearBuiltData();

	/** Regenerates the BuildCacheGuid, invalidating any cached build results for this Collection. */
	void RefreshBuildCacheGuid();

	/**
	 * Moves any built assets stored within this Collection to their own asset packages, making them
	 * standalone assets that can be referenced from other objects.
	 *
	 * The Collection can still be used as normal after this, as it will still reference the unpacked
	 * assets as it did before they were unpacked.
	 */
	void UnpackAssets(const FOnMetaHumanCharacterAssetsUnpacked& OnComplete = FOnMetaHumanCharacterAssetsUnpacked());
	
	/** 
	 * Sets the default Pipeline from the project settings.
	 * 
	 * Call this after constructing a Collection if you don't have a specific Pipeline to use.
	 */
	void SetDefaultPipeline();

	/**
	 * Set the Pipeline for this Collection to use.
	 * 
	 * It's not necessary to provide each Collection instance with a unique Pipeline instance, as the 
	 * Pipeline is intended to be stateless. However, the Pipeline instance will be editable in
	 * Details panels wherever the Collection is visible for editing, and users editing a Pipeline's
	 * properties from there may be surprised if these edits affect other Collections.
	 */
	void SetPipeline(UMetaHumanCollectionPipeline* InPipeline);
	
	/**
	 * Sets the Pipeline to be an instance of the given class
	 */
	void SetPipelineFromClass(TSubclassOf<UMetaHumanCollectionPipeline> InPipelineClass);

	/** Convenience function to access the editor pipeline */
	const UMetaHumanCollectionEditorPipeline* GetEditorPipeline() const;
		
	virtual const UMetaHumanCharacterEditorPipeline* GetPaletteEditorPipeline() const override;

	[[nodiscard]] virtual bool TryRemoveItem(const FMetaHumanPaletteItemKey& ExistingKey) override;

	/** 
	 * Copies the contents of the Other Collection into this Collection, discarding this 
	 * Collection's existing contents and leaving it in an unbuilt state.
	 * 
	 * Used for copying edits from a temporary Collection back to the persistent Collection.
	 * 
	 * Contents in this case refers to the Collection's items and its default MetaHuman Instance.
	 * 
	 * Note that some properties, such as Quality, are considered settings rather than contents and
	 * are intentionally not copied.
	 */
	void CopyContentsFrom(TNotNull<const UMetaHumanCollection*> Other);

	/**
	 * The Pipeline targeted by this Collection. 
	 * 
	 * May be null if the user hasn't set a pipeline yet.
	 */
	UMetaHumanCollectionPipeline* GetMutablePipeline();
#endif
	
	/**
	 * The Pipeline targeted by this Collection. 
	 * 
	 * May be null if the user hasn't set a pipeline yet.
	 */
	const UMetaHumanCollectionPipeline* GetPipeline() const;

	virtual const UMetaHumanCharacterPipeline* GetPalettePipeline() const override;

	/** Note that the returned data is not guaranteed to be valid. Call IsValid on the result to check. */
	const FMetaHumanCollectionBuiltData& GetBuiltData() const;

	UE_DEPRECATED(5.8, "Quality is now a property of the Collection. Use the GetBuiltData overload with no parameters.")
	const FMetaHumanCollectionBuiltData& GetBuiltData(EMetaHumanCharacterPaletteBuildQuality InQuality) const;

	/** Note that in any cooked Collection, Quality will always be Production. */
	EMetaHumanCharacterPaletteBuildQuality GetQuality() const;
	
#if WITH_EDITOR
	/** 
	 * The Collection contains a default instance that is used for preview. 
	 * 
	 * Guaranteed to be non-null.
	 */
	TNotNull<const UMetaHumanInstance*> GetDefaultInstance() const;

	TNotNull<UMetaHumanInstance*> GetMutableDefaultInstance();

	// Begin UObject interface
	virtual void PostInitProperties() override;
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	// End UObject interface
#endif // WITH_EDITOR

	/**
	 * Resolves virtual slots in the given array of selections.
	 * 
	 * For each selection in the array, if that selection targets a virtual slot, it will be 
	 * updated to target the underlying real slot.
	 * 
	 * Once this is done, code using the resulting array can just operate on real slots and doesn't
	 * have to handle virtual slots at all.
	 * 
	 * This function assumes that PipelineSpec is valid (i.e. PipelineSpec->IsValid() returns true),
	 * otherwise it may assert or never return.
	 * 
	 * Any selections that don't resolve to valid items in the Collection will be omitted from the 
	 * returned array.
	 */
	[[nodiscard]] TArray<FMetaHumanPipelineSlotSelectionData> PropagateVirtualSlotSelections(const TArray<FMetaHumanPipelineSlotSelectionData>& Selections) const;
	
#if WITH_EDITOR
	/** 
	 * Delegate fired when a new Pipeline has been set on this Collection.
	 * 
	 * This can only happen in editor.
	 * 
	 * If any changes need to be made to the Collection in response to the Pipeline changing, such as
	 * removing items from slots that don't exist on the new Pipeline, those changes will be made
	 * before this delegate is fired.
	 */
	FOnPipelineChanged OnPipelineChanged;
	
	/** Delegate fired when the Collection has finished building, if it succeeded */
	FOnCollectionBuilt OnCollectionBuilt;
#endif // WITH_EDITOR
	
#if WITH_EDITORONLY_DATA
	/**
	 * A GUID used in DDC keys made during this Collection's build.
	 *
	 * In theory Collections should be safe to all use the same DDC keys, but if the cached data
	 * needs to be rebuilt for any reason, this setup allows a Collection to rebuild only its own
	 * cache by regenerating this GUID.
	 */
	UPROPERTY()
	FGuid BuildCacheGuid;

	/** If true, this Collection's preview counterpart will be built automatically whenever an edit is made to it in the Collection editor */
	UPROPERTY()
	bool bAutoBuildForPreview = false;

	/** If false, only the currently selected items will be built for preview in the Collection editor. If true, all items will be built. */
	UPROPERTY()
	bool bBuildAllItemsForPreview = false;

	/** The mode for determining which folder to unpack the Collection's assets to */
	UPROPERTY(EditAnywhere, Category = "Targets")
	EMetaHumanCharacterUnpackPathMode UnpackPathMode = EMetaHumanCharacterUnpackPathMode::Relative;

	/** The folder path that assets will be unpacked to. Interpreted according to UnpackPathMode. */
	UPROPERTY(EditAnywhere, Category = "Targets", meta = (EditCondition = "UnpackPathMode != EMetaHumanCharacterUnpackPathMode::SubfolderNamedForPalette", EditConditionHides))
	FString UnpackFolderPath = TEXT("Unpacked");

	/** Returns the folder path where the assets will be unpacked, depending on the UnpackPathMode */
	FString GetUnpackFolder() const;
#endif // WITH_EDITORONLY_DATA

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UE_DEPRECATED(5.8, "OnPaletteBuilt has been replaced by OnCollectionBuilt")
	FOnPaletteBuilt OnPaletteBuilt;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

protected:
#if WITH_EDITOR
	virtual void OnItemsModified() override;
	virtual bool TryImportWardrobeItem(FName SlotName, TNotNull<const UMetaHumanWardrobeItem*> SourceWardrobeItem, FMetaHumanPaletteItemKey& OutNewItemKey) override;
#endif // WITH_EDITOR

private:
	// IMPORTANT: If you're adding a new property that would be considered contents, you *must* update CopyContentsFrom to handle it

#if WITH_EDITOR
	/**
	 * Returns the list of Collection Pipeline classes that should *not* be selectable in the Collection editor.
	 *
	 * This is used to exclude pipelines that are intended for use by MetaHuman Creator only.
	 */
	UFUNCTION()
	TArray<UClass*> GetDisallowedPipelineClasses() const;
#endif // WITH_EDITOR

	/** The MetaHuman Collection Pipeline used to build this collection */
	UPROPERTY(EditAnywhere, Instanced, Category = "Character", meta = (GetDisallowedClasses = "GetDisallowedPipelineClasses"))
	TObjectPtr<UMetaHumanCollectionPipeline> Pipeline;
	
#if WITH_EDITORONLY_DATA
	UPROPERTY(BlueprintReadOnly, Category = "Character", meta = (AllowPrivateAccess))
	TObjectPtr<UMetaHumanInstance> DefaultInstance;
#endif // WITH_EDITORONLY_DATA

	/** 
	 * IMPORTANT: All properties populated from Build should be marked DuplicateTransient and must
	 * be reset by the ClearBuiltData function.
	 *
	 * This is because built data is defined by the pipeline and can't be guaranteed to be 
	 * duplicated correctly.
	 */ 

	/** The data produced by the Collection Pipeline during the last successful call to Build. */
	UPROPERTY(DuplicateTransient)
	FMetaHumanCollectionBuiltData BuiltData;

	/** 
	 * The build quality that will be used by this Collection.
	 * 
	 * Note that Preview Collections are intended to be transient and shouldn't be saved into 
	 * persistent storage. They will intentionally fail to cook, in order to help prevent 
	 * accidental misuse.
	 */
	UPROPERTY()
	EMetaHumanCharacterPaletteBuildQuality Quality = EMetaHumanCharacterPaletteBuildQuality::Production;

	/** True if the assets in this Collection have been unpacked and are in their own packages. */
	UPROPERTY(DuplicateTransient)
	bool bIsUnpacked;
};
