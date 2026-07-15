// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "StructUtils/PropertyBag.h"
#include "Templates/SubclassOf.h"

#include "MetaHumanPaletteItemKey.h"
#include "MetaHumanPaletteItemPath.h"
#include "MetaHumanPipelineSlotSelection.h"

#include "MetaHumanCollectionBlueprintLibrary.generated.h"

#define UE_API METAHUMANCHARACTERPALETTE_API

class UMetaHumanCharacterPalette;
class UMetaHumanCharacterPipelineSpecification;
class UMetaHumanCollection;
class UMetaHumanInstance;
class UMetaHumanWardrobeItem;

/**
 * @brief Exposes blueprint functions to operate on FMetaHumanPaletteItemKey.
 *
 * Functions are tagged with ScriptMethod so they can be called directly on the struct without
 * the need to reference the library.
 */
UCLASS(MinimalAPI)
class UMetaHumanPaletteKeyBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * @brief Returns true if the other key is identical to this one except for Variation
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Item", meta = (ScriptMethod))
	static UE_API bool ReferencesSameAsset(const FMetaHumanPaletteItemKey& InKey, const FMetaHumanPaletteItemKey& InOther);

	/**
	 * @brief Produces a string suitable for using as part of an asset name
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Item", meta = (ScriptMethod))
	static UE_API FString ToAssetNameString(const FMetaHumanPaletteItemKey& InKey);

	/**
	 * @brief Returns true if the key is the null item key (does not reference any asset).
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Item", meta = (ScriptMethod))
	static UE_API bool IsNull(const FMetaHumanPaletteItemKey& InKey);
};


/**
 * @brief Exposes blueprint functions to operate on FMetaHumanPaletteItemPath.
 */
UCLASS(MinimalAPI)
class UMetaHumanPaletteItemPathBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * @brief Constructs a FMetaHumanPaletteItemPath from a FMetaHumanPaletteItemKey.
	 *
	 * Exposed as a Make function for FMetaHumanPaletteItemPath.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Item", meta = (NativeMakeFunc))
	static UE_API FMetaHumanPaletteItemPath MakeItemPath(const FMetaHumanPaletteItemKey& InItemKey);
};


/**
 * The types of parameters supported by Wardrobe Items.
 *
 * These map directly to EPropertyBagPropertyType but since not all values are exposed
 * (e.g. Struct is hidden) we have a mapped enum to keep the surface area scoped.
 */
UENUM()
enum class EMetaHumanCharacterInstanceParameterType : uint8
{
	None		= (uint8) EPropertyBagPropertyType::None		UMETA(Hidden),
	Bool		= (uint8) EPropertyBagPropertyType::Bool,
	Float		= (uint8) EPropertyBagPropertyType::Float,
	Name		= (uint8) EPropertyBagPropertyType::Name,
	String		= (uint8) EPropertyBagPropertyType::String,
	Color		= (uint8) EPropertyBagPropertyType::Struct,
	Object		= (uint8) EPropertyBagPropertyType::Object,
	SoftObject	= (uint8) EPropertyBagPropertyType::SoftObject,
};

/**
 * @brief Struct that represents a parameter of a particular item in a collection.
 *
 * This struct is designed to work in scripting environments and has functions to get and set
 * values exposed via UMetaHumanCharacterInstanceParameterBlueprintLibrary.
 *
 * Note that calling one of the Set functions automatically updates the parameter value for the
 * item it represents on the instance that was used when querying them.
 */
USTRUCT(BlueprintType)
struct FMetaHumanCharacterInstanceParameter
{
	GENERATED_BODY()

	/** The name of the parameter. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MetaHuman|Parameters")
	FName Name;

	/** The type of the parameter. Use this to decide which Set/Get function to call. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MetaHuman|Parameters")
	EMetaHumanCharacterInstanceParameterType Type = EMetaHumanCharacterInstanceParameterType::None;

	/** The item path that this parameter belongs to. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MetaHuman|Parameters")
	FMetaHumanPaletteItemPath ItemPath;

	/**
	 * For Object and SoftObject parameters, the class the property is constrained to.
	 *
	 * Useful for telling whether a particular Object parameter is asking for a UTexture2D, a
	 * UMaterialInterface, etc, before deciding what to set on it.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MetaHuman|Parameters")
	TObjectPtr<UClass> ObjectClass = nullptr;

	/** Pointer back to the instance this parameter was queried from. */
	UPROPERTY()
	TWeakObjectPtr<UMetaHumanInstance> Instance;
};


/**
 * @brief Provides Get/Set functions for FMetaHumanCharacterInstanceParameter.
 *
 * Calling a Set function applies the new value to the instance that the parameter was queried
 * from, immediately.
 */
UCLASS(MinimalAPI)
class UMetaHumanCharacterInstanceParameterBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Parameters", meta = (ScriptMethod = "GetBool", Keywords = "GetBool"))
	static UE_API bool GetBoolInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, bool& OutValue);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Parameters", meta = (ScriptMethod = "SetBool", Keywords = "SetBool"))
	static UE_API bool SetBoolInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, bool InValue);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Parameters", meta = (ScriptMethod = "GetFloat", Keywords = "GetFloat GetScalar"))
	static UE_API bool GetFloatInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, float& OutValue);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Parameters", meta = (ScriptMethod = "SetFloat", Keywords = "SetFloat SetScalar"))
	static UE_API bool SetFloatInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, float InValue);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Parameters", meta = (ScriptMethod = "GetName", Keywords = "GetName"))
	static UE_API bool GetNameInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, FName& OutValue);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Parameters", meta = (ScriptMethod = "SetName", Keywords = "SetName"))
	static UE_API bool SetNameInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, FName InValue);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Parameters", meta = (ScriptMethod = "GetString", Keywords = "GetString"))
	static UE_API bool GetStringInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, FString& OutValue);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Parameters", meta = (ScriptMethod = "SetString", Keywords = "SetString"))
	static UE_API bool SetStringInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, const FString& InValue);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Parameters", meta = (ScriptMethod = "GetColor", Keywords = "GetColor GetVector"))
	static UE_API bool GetColorInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, FLinearColor& OutValue);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Parameters", meta = (ScriptMethod = "SetColor", Keywords = "SetColor SetVector"))
	static UE_API bool SetColorInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, const FLinearColor& InValue);

	/**
	 * Get the object value of an Object-typed parameter.
	 *
	 * @param OutValue Receives the current value, possibly nullptr if the parameter is unset.
	 * @return true if the read succeeded, false on type mismatch or if the parameter could not
	 *         be located on the instance.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Parameters", meta = (ScriptMethod = "GetObject", Keywords = "GetObject GetTexture"))
	static UE_API bool GetObjectInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, UObject*& OutValue);

	/**
	 * Set the object value of an Object-typed parameter.
	 *
	 * The provided object's class must derive from FMetaHumanCharacterInstanceParameter::ObjectClass.
	 * Pass nullptr to clear the parameter.
	 *
	 * @return true if the write succeeded.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Parameters", meta = (ScriptMethod = "SetObject", Keywords = "SetObject SetTexture"))
	static UE_API bool SetObjectInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, UObject* InValue);

	/**
	 * Get the soft-object path of a SoftObject-typed parameter.
	 *
	 * @param OutValue Receives the current path, which may be empty if the parameter is unset.
	 * @return true if the read succeeded.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Parameters", meta = (ScriptMethod = "GetSoftObject", Keywords = "GetSoftObject"))
	static UE_API bool GetSoftObjectInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, TSoftObjectPtr<UObject>& OutValue);

	/**
	 * Set the soft-object value of a SoftObject-typed parameter.
	 *
	 * The referenced asset's class (when loaded) must derive from
	 * FMetaHumanCharacterInstanceParameter::ObjectClass. Pass an empty soft-pointer to clear.
	 *
	 * @return true if the write succeeded.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Parameters", meta = (ScriptMethod = "SetSoftObject", Keywords = "SetSoftObject"))
	static UE_API bool SetSoftObjectInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, const TSoftObjectPtr<UObject>& InValue);
};


/**
 * @brief Exposes blueprint functions to operate on FMetaHumanPipelineSlotSelection.
 */
UCLASS(MinimalAPI)
class UMetaHumanPipelineSlotSelectionBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Construct a slot selection for a virtual slot on the Collection itself.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Pipeline", meta = (NativeMakeFunc))
	static UE_API FMetaHumanPipelineSlotSelection MakeSlotSelection(FName SlotName, const FMetaHumanPaletteItemKey& SelectedItem, FMetaHumanPaletteItemPath ParentItemPath = FMetaHumanPaletteItemPath());

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Pipeline", meta = (ScriptMethod))
	static UE_API FMetaHumanPaletteItemPath GetSelectedItemPath(const FMetaHumanPipelineSlotSelection& InSlotSelection);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Pipeline", meta = (ScriptMethod))
	static UE_API FName GetSelectedSlotName(const FMetaHumanPipelineSlotSelection& InSlotSelection);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Pipeline", meta = (ScriptMethod))
	static UE_API FMetaHumanPaletteItemKey GetSelectedItemKey(const FMetaHumanPipelineSlotSelection& InSlotSelection);
};


/**
 * @brief Exposes blueprint functions to enumerate the contents of a UMetaHumanCharacterPalette,
 *        which is the base class of both UMetaHumanCollection and UMetaHumanWardrobeItem.
 */
UCLASS(MinimalAPI)
class UMetaHumanCollectionBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Returns the pipeline specification for the palette's pipeline. May be null if no pipeline
	 * is set or the pipeline doesn't expose a specification.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Palette", meta = (ScriptMethod))
	static UE_API UMetaHumanCharacterPipelineSpecification* GetPipelineSpecification(UMetaHumanCharacterPalette* Palette);

	/**
	 * Returns the virtual slot names defined by the palette's pipeline.
	 *
	 * Slot selections on a MetaHuman Instance should target virtual slots; real (backing) slots
	 * are an implementation detail of the pipeline.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Palette", meta = (ScriptMethod))
	static UE_API TArray<FName> GetSlotNames(UMetaHumanCharacterPalette* Palette);

	/** Returns the keys of every item in the palette. */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Palette", meta = (ScriptMethod))
	static UE_API TArray<FMetaHumanPaletteItemKey> GetAllItemKeys(UMetaHumanCharacterPalette* Palette);

	/**
	 * Returns the keys of items targeting the given virtual slot.
	 *
	 * The slot name is resolved through the pipeline so callers can safely pass virtual slot
	 * names (the same names returned by GetSlotNames).
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Palette", meta = (ScriptMethod))
	static UE_API TArray<FMetaHumanPaletteItemKey> GetItemKeysForSlot(UMetaHumanCharacterPalette* Palette, FName SlotName);

	/**
	 * Returns the keys of items in the palette whose principal asset is the given asset.
	 *
	 * Multiple results are possible if the palette contains multiple Variations of the same
	 * asset.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Palette", meta = (ScriptMethod))
	static UE_API TArray<FMetaHumanPaletteItemKey> GetItemKeysForPrincipalAsset(UMetaHumanCharacterPalette* Palette, UObject* PrincipalAsset);

	/**
	 * Returns the keys of items in the palette that reference the given external wardrobe item.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Palette", meta = (ScriptMethod))
	static UE_API TArray<FMetaHumanPaletteItemKey> GetItemKeysForWardrobeItem(UMetaHumanCharacterPalette* Palette, UMetaHumanWardrobeItem* WardrobeItem);

	/** Returns the slot name an item is authored against, or NAME_None if the key is not found. */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Palette", meta = (ScriptMethod))
	static UE_API FName GetItemSlotName(UMetaHumanCharacterPalette* Palette, const FMetaHumanPaletteItemKey& ItemKey);

#if WITH_EDITOR
	/** Returns the display name for an item, or empty text if the key is not found. */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Palette", meta = (ScriptMethod))
	static UE_API FText GetItemDisplayName(UMetaHumanCharacterPalette* Palette, const FMetaHumanPaletteItemKey& ItemKey);
#endif // WITH_EDITOR
};


/**
 * @brief Exposes blueprint functions to operate on UMetaHumanInstance for procedural authoring.
 */
UCLASS(MinimalAPI)
class UMetaHumanCharacterInstanceBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Creates a new transient UMetaHumanInstance that is an unassembled duplicate of the source
	 *
	 * @param Source         The instance to duplicate. If null, returns null.
	 * @param Outer          Optional outer for the new instance. If null, the transient package is used.
	 * @return The new instance, or null if Source was null.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Instance")
	static UE_API UMetaHumanInstance* DuplicateMetaHumanInstance(UMetaHumanInstance* Source, UObject* Outer = nullptr);

	/**
	 * @brief Gets the list of all instance parameters for a given item.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Instance", meta = (ScriptMethod = "GetInstanceParameters"))
	static UE_API TArray<FMetaHumanCharacterInstanceParameter> GetInstanceParametersForItem(UMetaHumanInstance* InInstance, const FMetaHumanPaletteItemPath& ItemPath);

	/**
	 * Returns every item path on this instance that has any instance parameters defined.
	 *
	 * The empty path represents the Collection itself.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Instance", meta = (ScriptMethod))
	static UE_API TArray<FMetaHumanPaletteItemPath> GetInstanceParameterItemPaths(UMetaHumanInstance* InInstance);

	/**
	 * Look up a single parameter on the instance by item path and name.
	 *
	 * @return true if found.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Instance", meta = (ScriptMethod))
	static UE_API bool TryGetInstanceParameter(UMetaHumanInstance* InInstance, const FMetaHumanPaletteItemPath& ItemPath, FName ParameterName, FMetaHumanCharacterInstanceParameter& OutParameter);

	/**
	 * Returns the item keys targeting the given slot that are allowed by the Instance's Collection
	 * Pipeline given the Instance's current slot selections.
	 *
	 * Use this to ask "with what I've already picked, which items can I now select for SlotName?".
	 * For example, after picking a body, this can return the heads compatible with that body.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Instance", meta = (ScriptMethod))
	static UE_API TArray<FMetaHumanPaletteItemKey> GetAllowedItemKeysForSlot(UMetaHumanInstance* InInstance, FName SlotName);
};

#undef UE_API
