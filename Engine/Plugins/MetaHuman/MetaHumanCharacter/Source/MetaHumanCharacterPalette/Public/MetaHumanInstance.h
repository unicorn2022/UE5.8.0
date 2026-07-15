// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCollectionPipeline.h"
#include "MetaHumanPaletteItemPath.h"
#include "MetaHumanPipelineSlotSelection.h"
#include "MetaHumanPipelineSlotSelectionData.h"
#include "MetaHumanPostAssemblyParameterOutputCollection.h"

#include "HAL/IConsoleManager.h"
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/Object.h"

#include "MetaHumanInstance.generated.h"

struct FMetaHumanPaletteItemKey;
struct FMetaHumanPipelineSlotSelection;
class UMetaHumanCollection;

UENUM()
enum class EMetaHumanCharacterAssemblyResult : uint8
{
	Succeeded,
	Failed
};

UENUM(meta = (Bitflags))
enum class EMetaHumanInstanceParameterOverrideResult : uint8
{
	NoParametersWereModified = 0,

	ReassemblyRequiredToApplyNewParameters 	= 1 << 0,
	PostAssemblyParametersWereModified 		= 1 << 1,
	UnrecognizedParametersWereStored		= 1 << 2
};
ENUM_CLASS_FLAGS(EMetaHumanInstanceParameterOverrideResult)

/**
 * Determines whether a MetaHuman Instance should be assembled at cook time and have its
 * assembly output baked into the cooked package.
 */
UENUM()
enum class EMetaHumanInstanceCookBehavior : uint8
{
	/**
	 * Defer to the Collection Editor Pipeline's ShouldCookInstanceAsAssembled function to decide
	 * whether to assemble the Instance at cook time.
	 */
	PipelineDefault,

	/** Always assemble the Instance at cook time. */
	Yes,

	/** Never assemble the Instance at cook time. */
	No
};

/** The output of an assembly and its associated data */
USTRUCT()
struct FMetaHumanProcessedAssemblyOutput
{
	GENERATED_BODY()

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// Explicit defaults required because of the deprecated UPROPERTY below.
	FMetaHumanProcessedAssemblyOutput() = default;
	~FMetaHumanProcessedAssemblyOutput() = default;
	FMetaHumanProcessedAssemblyOutput(const FMetaHumanProcessedAssemblyOutput&) = default;
	FMetaHumanProcessedAssemblyOutput(FMetaHumanProcessedAssemblyOutput&&) = default;
	FMetaHumanProcessedAssemblyOutput& operator=(const FMetaHumanProcessedAssemblyOutput&) = default;
	FMetaHumanProcessedAssemblyOutput& operator=(FMetaHumanProcessedAssemblyOutput&&) = default;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	/** Returns true if there's a valid assembly output stored in this struct. */
	bool IsValid() const
	{
		return AssemblyOutput.IsValid();
	}

	/*
	 * A structure produced by the Character Pipeline that contains the assets belonging to this
	 * instance, such as meshes and materials.
	 *
	 * The type of this struct is guaranteed to be Collection->GetPipeline()->GetSpecification()->AssemblyOutputStruct
	 */
	UPROPERTY()
	FInstancedStruct AssemblyOutput;

	/** The unmodified set of Post-Assembly Parameters produced by the most recent assembly */
	UPROPERTY()
	FMetaHumanPostAssemblyParameterOutputCollection PostAssemblyParametersFromPipeline;

	UE_DEPRECATED(5.8, "This is only used to process the deprecated form of Instance Parameters")
	UPROPERTY()
	TSet<FMetaHumanPaletteItemPath> ItemsUsingDeprecatedInstanceParameters;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FMetaHumanGeneratedAssetMetadata> AssemblyAssetMetadata;
#endif
};

/** 
 * Determines how pipeline slots that don't have an item selected for them should be handled when 
 * the Instance is converted to a set of pinned slot selections.
 */
enum class EMetaHumanUnusedSlotBehavior : uint8
{
	/** 
	 * Unused slots should be left unpinned.
	 * 
	 * In the built Collection, the user will be able to assign these slots using an Instance.
	 */
	Unpinned,

	/** Unused slots should be "pinned to empty", so they will not be assignable by an Instance. */
	PinnedToEmpty
};

DECLARE_DYNAMIC_DELEGATE_OneParam(FMetaHumanCharacterAssembled, EMetaHumanCharacterAssemblyResult, Result);
DECLARE_DELEGATE_OneParam(FMetaHumanCharacterAssembledNative, EMetaHumanCharacterAssemblyResult /* Result */);

DECLARE_MULTICAST_DELEGATE(FMetaHumanInstanceUpdatedNative);

/**
 * Used to assemble a renderable character from a MetaHuman Collection.
 * 
 * Can be either an asset used in the editor or a transient object generated at runtime.
 */
UCLASS(BlueprintType)
class METAHUMANCHARACTERPALETTE_API UMetaHumanInstance : public UObject
{
	GENERATED_BODY()

public:
	/** 
	 * Runs the associated Character Pipeline's assembly function to populate the AssemblyOutput. 
	 * 
	 * Fails gracefully if no MetaHuman Collection is set.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Instance")
	void Assemble(const FMetaHumanCharacterAssembled& OnAssembled);
	void Assemble(const FMetaHumanCharacterAssembledNative& OnAssembledNative = FMetaHumanCharacterAssembledNative());
	void Assemble(const FMetaHumanCharacterAssembled& OnAssembled, const FMetaHumanCharacterAssembledNative& OnAssembledNative);

	UE_DEPRECATED(5.8, "Quality is now a property of the Collection. This overload will be removed.")
	void Assemble(EMetaHumanCharacterPaletteBuildQuality Quality, const FMetaHumanCharacterAssembled& OnAssembled);
	UE_DEPRECATED(5.8, "Quality is now a property of the Collection. This overload will be removed.")
	void Assemble(EMetaHumanCharacterPaletteBuildQuality Quality, const FMetaHumanCharacterAssembledNative& OnAssembledNative = FMetaHumanCharacterAssembledNative());
	UE_DEPRECATED(5.8, "Quality is now a property of the Collection. This overload will be removed.")
	void Assemble(EMetaHumanCharacterPaletteBuildQuality Quality, const FMetaHumanCharacterAssembled& OnAssembled, const FMetaHumanCharacterAssembledNative& OnAssembledNative);

	/** Assemble if necessary and return the assembly output */
	UFUNCTION(BlueprintPure, Category = "MetaHuman|Instance")
	const FInstancedStruct& GetAssemblyOutput();

	UE_DEPRECATED(5.8, "GetAssemblyOutput has been made non-const. Use GetExistingAssemblyOutput to get the assembly output without the possibility of triggering assembly.")
	const FInstancedStruct& GetAssemblyOutput() const;

	/** 
	 * GetAssemblyOutput should be used instead of this function in most cases.
	 * 
	 * Returns the existing assembly output, if any.
	 * 
	 * This is useful if you're in a const-only context and are triggering assembly via a different
	 * path, or deliberately want to avoid triggering assembly if it hasn't been done yet.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Instance")
	const FInstancedStruct& GetExistingAssemblyOutput() const;

	/** Returns true if the Instance is assembled and up to date */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Instance")
	bool IsAssembled() const;

	/**
	 * Clear the result of the last assembly.
	 * 
	 * GetAssemblyOutput will return an empty struct after calling this.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Instance")
	void ClearAssemblyOutput();

	/** 
	 * Set the MetaHuman Collection that this instance will assemble from.
	 * 
	 * Call with nullptr to clear the existing Collection.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Instance")
	void SetMetaHumanCollection(UMetaHumanCollection* InCollection);

	/** 
	 * Return the MetaHuman Collection that this instance will assemble from.
	 * 
	 * Returns nullptr if no collection has been set.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Instance")
	UMetaHumanCollection* GetMetaHumanCollection() const;

	/** 
	 * Copies the contents of the Other Instance into this Instance, discarding this Instance's
	 * existing contents and leaving it in an unassembled state.
	 * 
	 * Used for copying edits from a temporary Instance back to the persistent Instance.
	 * 
	 * Contents in this case refers to the Instance's slot selections and Instance parameters.
	 * 
	 * This instance will be set to point to the Other Instance's Collection. If this is not 
	 * desired you can call SetMetaHumanCollection afterwards to change it.
	 */
	void CopyContentsFrom(TNotNull<const UMetaHumanInstance*> Other);

	/** 
	 * Remove any existing selections for this slot and select only the given item.
	 * 
	 * If ItemKey is NAME_None, no item will be selected for this slot.
	 */
	UFUNCTION(BlueprintCallable, Category = "Slots")
	void SetSingleSlotSelection(FName SlotName, const FMetaHumanPaletteItemKey& ItemKey);
	void SetSingleSlotSelection(const FMetaHumanPaletteItemPath& ParentItemPath, FName SlotName, const FMetaHumanPaletteItemKey& ItemKey);

	// Adds the provided slot selection if valid, e.g. won't allow duplicate selections or multiple selections for slots that don't allow it
	UFUNCTION(BlueprintCallable, Category = Instance)
	[[nodiscard]] bool TryAddSlotSelection(const FMetaHumanPipelineSlotSelection& Selection);

	/** 
	 * Get a single item selection for this slot, if there is at least one.
	 * 
	 * If there are multiple selections for this slot, this function returns an arbitrary selection
	 * and repeated calls may return a different selection.
	 */
	[[nodiscard]] bool TryGetAnySlotSelection(FName SlotName, FMetaHumanPaletteItemKey& OutItemKey) const;
	[[nodiscard]] bool TryGetAnySlotSelection(const FMetaHumanPaletteItemPath& ParentItemPath, FName SlotName, FMetaHumanPaletteItemKey& OutItemKey) const;

	[[nodiscard]] static bool TryGetAnySlotSelection(
		TConstArrayView<FMetaHumanPipelineSlotSelectionData> SlotSelections, 
		FName SlotName, 
		FMetaHumanPaletteItemKey& OutItemKey);

	[[nodiscard]] static bool TryGetAnySlotSelection(
		TConstArrayView<FMetaHumanPipelineSlotSelectionData> SlotSelections, 
		const FMetaHumanPaletteItemPath& ParentItemPath, 
		FName SlotName, 
		FMetaHumanPaletteItemKey& OutItemKey);

	[[nodiscard]] static bool TryGetAnySlotSelection(
		TConstArrayView<FMetaHumanPipelineSlotSelection> SlotSelections, 
		FName SlotName, 
		FMetaHumanPaletteItemKey& OutItemKey);

	[[nodiscard]] static bool TryGetAnySlotSelection(
		TConstArrayView<FMetaHumanPipelineSlotSelection> SlotSelections, 
		const FMetaHumanPaletteItemPath& ParentItemPath, 
		FName SlotName, 
		FMetaHumanPaletteItemKey& OutItemKey);

	bool ContainsSlotSelection(const FMetaHumanPipelineSlotSelection& Selection) const;
	// Returns true if the selection existed and was removed, false if it didn't exist
	bool TryRemoveSlotSelection(const FMetaHumanPipelineSlotSelection& Selection);

	UFUNCTION(BlueprintCallable, Category = "Slots")
	const TArray<FMetaHumanPipelineSlotSelectionData>& GetSlotSelectionData() const;

	/** 
	 * Formats the slot selections and overridden instance parameters stored in this instance to be
	 * passed into a Collection build as pinned selections.
	 */
	TArray<FMetaHumanPinnedSlotSelection> ToPinnedSlotSelections(EMetaHumanUnusedSlotBehavior UnusedSlotBehavior) const;

	// The term Instance Parameters now refers to both Assembly and Post-Assembly Parameters.
	//
	// In other words, Assembly and Post-Assembly Parameters are types of Instance Parameter.
	UE_DEPRECATED(5.8, "There are now two types of Instance Parameters: Assembly Parameters and Post-Assembly Parameters, with corresponding functions")
	TMap<FMetaHumanPaletteItemPath, FInstancedPropertyBag> GetAssemblyInstanceParameters() const;

	/** 
	 * Returns the Assembly Parameters produced by the last build and their original values.
	 * 
	 * Assembly Parameters are values that can be set at assembly time.
	 * 
	 * Although they come from the build output, this function is provided here for convenience.
	 * 
	 * The parameters for the empty item path are the Collection's own parameters.
	 */
	TMap<FMetaHumanPaletteItemPath, FInstancedPropertyBag> GetAssemblyParameters() const;
	
	/** 
	 * Returns the Post-Assembly Parameters produced by the last assembly and their original values.
	 * 
	 * Post-Assembly Parameters are values that can be set after assembly, such as material 
	 * parameters. 
	 * 
	 * They are strictly a subset of Assembly Parameters. In other words, every Post-Assembly 
	 * Parameter is also an Assembly Parameter but not vice versa.
	 * 
	 * The parameters for the empty item path are the Collection's own parameters.
	 */
	TMap<FMetaHumanPaletteItemPath, FInstancedPropertyBag> GetPostAssemblyParameters() const;

	/** 
	 * Returns the Instance Parameters stored in this instance as overrides.
	 * 
	 * These overrides will be applied to any assembly created through this instance.
	 * 
	 * They may include overrides for items and parameters that don't exist on the current assembly.
	 * These persist indefinitely until explicitly cleared.
	 */
	TMap<FMetaHumanPaletteItemPath, FInstancedPropertyBag> GetOverriddenInstanceParameters() const;

	/**
	 * Returns the Instance Parameters produced by the last assembly and their effective values.
	 * 
	 * If a parameter has an override value (from GetOverriddenInstanceParameters), it will be set 
	 * to this override value. Otherwise, it will be set to the original value from the assembly
	 * (from GetAssemblyInstanceParameters).
	 */
	FInstancedPropertyBag GetCurrentInstanceParametersForItem(const FMetaHumanPaletteItemPath& ItemPath) const;

	/**
	 * Set the overridden Instance Parameter values for a given item, or the Collection itself if
	 * an empty item path is specified.
	 * 
	 * Overridden values set via this function will persist indefinitely until explicitly cleared.
	 * 
	 * These parameter values will be applied immediately if there's a valid assembly. If Assemble 
	 * is called after this and a new assembly is created, they will automatically be applied to 
	 * that assembly and any future assemblies.
	 */
	EMetaHumanInstanceParameterOverrideResult OverrideInstanceParameters(const FMetaHumanPaletteItemPath& ItemPath, const FInstancedPropertyBag& NewInstanceParameterValues);

	/** Functions to clear overridden Instance Parameters */
	void ClearAllOverriddenInstanceParameters();
	void ClearOverriddenInstanceParameters(const FMetaHumanPaletteItemPath& ItemPath);

#if WITH_EDITOR
	/** 
	 * Unpacks only the assets contained in this Instance.
	 * 
	 * Does not unpack assets from the Collection that this Instance depends on.
	 * 
	 * This function is intended for internal use and may change or be removed in future.
	 */
	bool TryUnpack(const FString& TargetFolder);

	/** 
	 * Notify users of this Instance that they should call GetAssemblyOutput again and update 
	 * themselves.
	 * 
	 * This is an editor-only mechanism to handle cases such as notifying actors placed in the 
	 * world that a MetaHuman Instance they reference has been modified.
	 * 
	 * Editor code that modifies an Instance should call this when its edits are done. This is
	 * deliberately not automatically triggered on every edit, because edits are often grouped
	 * together and we don't want to repeatedly notify objects while the Instance is still in 
	 * the process of being modified.
	 * 
	 * Project code that generates transient Instances and modifies them at runtime should have
	 * its own system for notifying any objects that need to know.
	 */
	void NotifyAssemblyOutputInvalidated();

	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

	// UObject interface
	virtual void PostLoad() override;

	UE_DEPRECATED(5.8, "This delegate is not robust and will be removed")
	mutable FMetaHumanInstanceUpdatedNative OnInstanceUpdatedNative;

private:
#if WITH_EDITOR
	/** Removes entries from OverriddenInstanceParameters whose keys don't resolve to items in the Collection */
	void RemoveStaleOverriddenInstanceParameters();

	/**
	 * Resolves ShouldCookAsAssembled to a concrete Yes/No, consulting the Collection's editor
	 * pipeline if the value is PipelineDefault.
	 *
	 * Returns the unresolved value (which may still be PipelineDefault) if the editor pipeline
	 * isn't reachable.
	 */
	EMetaHumanInstanceCookBehavior ResolveShouldCookAsAssembled() const;

	/** Updates WillCookAsAssembled based on the current ShouldCookAsAssembled value. */
	void RefreshWillCookAsAssembled();

	/**
	 * Walks references from the editor assembly output and marks all reached subobjects of this
	 * Instance as RF_Transient, so they don't get saved into the package.
	 *
	 * Called on a successful Assemble in the editor (skipped during cook commandlet).
	 */
	void MarkAssemblyOutputSubobjectsAsTransient();

	/**
	 * Walks references from the editor assembly output, marks reached subobjects of this Instance
	 * as RF_Transient and renames them into the transient package with auto-generated names. Used
	 * to fully detach the previous assembly's subobjects so a new assembly is free to reuse their
	 * names.
	 *
	 * Called from ClearAssemblyOutput in the editor (skipped during cook commandlet).
	 */
	void MoveAssemblyOutputSubobjectsToTransientPackage();
#endif // WITH_EDITOR

	/**
	 * Validate this Instance's slot selections and correct any invalid data.
	 * 
	 * If bShouldRemoveMissingItemSelections is true, selections that reference missing items will 
	 * be removed.
	 */
	void ValidateAndSanitizeSlotSelections(bool bShouldRemoveMissingItemSelections);

	// Legacy function to support item pipelines still using the deprecated Instance Parameters
	EMetaHumanInstanceParameterOverrideResult ApplyOverriddenInstanceParameters(const FMetaHumanPaletteItemPath& ItemPath) const;

	/**
	 * Returns a reference to the active assembly output, either EditorAssemblyOutput or 
	 * RuntimeAssemblyOutput depending on the context.
	 */
	const FMetaHumanProcessedAssemblyOutput& GetActiveAssemblyOutput() const;
	FMetaHumanProcessedAssemblyOutput& GetActiveAssemblyOutput();

	/**
	 * Returns true if any edit operations are permitted on the Instance.
	 * 
	 * DebugOperationName should be set to the name of the operation the caller wants to perform,
	 * to be used in debug logging if the edit is denied.
	 */
	bool CanModifyInstance(const TCHAR* DebugOperationName) const;

#if WITH_EDITORONLY_DATA
	/**
	 * The assembly output and associated data for this Instance while it's being edited in the
	 * editor.
	 * 
	 * Transient so that it's always freshly recreated on load and there's no chance of stale 
	 * references being kept to the Collection's built data, or of other assets saving references
	 * to this assembly output into their packages.
	 */
	UPROPERTY(Transient, DuplicateTransient)
	FMetaHumanProcessedAssemblyOutput EditorAssemblyOutput;
#endif

	/**
	 * The assembly output and associated data for this Instance in cooked builds.
	 * 
	 * Should be unpopulated in editor.
	 * 
	 * If bIsAssemblyOutputCooked is true, this data was created during cook and the Instance can't
	 * be modified or re-assembled at runtime.
	 */
	UPROPERTY(DuplicateTransient)
	FMetaHumanProcessedAssemblyOutput RuntimeAssemblyOutput;

	/**
	 * True if this Instance was loaded from a cooked package and has a valid baked
	 * RuntimeAssemblyOutput. When true, edits and re-assembly are not permitted.
	 */
	UPROPERTY(Transient, DuplicateTransient)
	bool bIsAssemblyOutputCooked = false;

	/**
	 * Determines whether this Instance should be assembled at cook time and have its assembly
	 * output baked into the cooked package.
	 */
	UPROPERTY(EditAnywhere, Category = Instance)
	EMetaHumanInstanceCookBehavior ShouldCookAsAssembled = EMetaHumanInstanceCookBehavior::PipelineDefault;

#if WITH_EDITORONLY_DATA
	/**
	 * The effective resolved value of ShouldCookAsAssembled.
	 * 
	 * The purpose of this property is purely to display in the editor UI what the cook behavior 
	 * will be. It has no effect on any logic.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = Instance)
	EMetaHumanInstanceCookBehavior WillCookAsAssembled = EMetaHumanInstanceCookBehavior::PipelineDefault;
#endif

	/** Used by the assembly that's currently in progress */
	UPROPERTY(Transient, DuplicateTransient)
	FMetaHumanInstanceParameterCollection CurrentOverriddenAssemblyParameters;

	UPROPERTY()
	TMap<FMetaHumanPaletteItemPath, FInstancedPropertyBag> OverriddenInstanceParameters;

	/**
	 * OverriddenInstanceParameters for the Collection pipeline itself, so that we can detect when 
	 * a key in OverriddenInstanceParameters has been reset to the empty path due to a missing asset.
	 */
	UPROPERTY()
	FInstancedPropertyBag OverriddenCollectionInstanceParameters;

	/** 
	 * The selected items for slots on the Pipeline.
	 * 
	 * Slots with no selection will select the default item for the slot.
	 * 
	 * Some slots allow multiple selections. This is determined by the UMetaHumanCharacterPipelineSpecification.
	 * 
	 * The order of items in this array has no significance. Consider it an unordered set.
	 */
	UPROPERTY()
	TArray<FMetaHumanPipelineSlotSelectionData> SlotSelections;

	UPROPERTY()
	TObjectPtr<UMetaHumanCollection> Collection;
};

using UMetaHumanCharacterInstance UE_DEPRECATED(5.8, "UMetaHumanCharacterInstance has been renamed to UMetaHumanInstance") = UMetaHumanInstance;
