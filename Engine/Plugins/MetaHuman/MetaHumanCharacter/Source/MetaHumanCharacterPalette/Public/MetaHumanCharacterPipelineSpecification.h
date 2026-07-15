// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Misc/NotNull.h"

#include "MetaHumanCharacterPipelineSpecification.generated.h"

class UMetaHumanCharacterPipelineSpecification;

#define UE_API METAHUMANCHARACTERPALETTE_API

namespace UE::MetaHuman::CharacterPipelineSlots
{
/**
 * A pipeline that accepts MetaHuman Characters should have a slot for them with this name, to
 * ensure compatibility with the MetaHuman Character editor.
 */
extern UE_API const FName Character;

/** 
 * Suggested slot names to improve compatibility between pipelines.
 * 
 * These names are not hard requirements, but using the same names for the same types of slot makes
 * it easier to migrate content across pipelines.
 */
extern UE_API const FName Hair;
extern UE_API const FName Eyebrows;
extern UE_API const FName Beard;
extern UE_API const FName Mustache;
extern UE_API const FName Eyelashes;
extern UE_API const FName Peachfuzz;
}

/**
 * Determines the interpretation of FMetaHumanAssemblyOutputMapping.
 * 
 * See FMetaHumanAssemblyOutputMapping for more details.
 */
UENUM()
enum class EMetaHumanAssemblyOutputMappingMethod : uint8
{
	/**
	 * No automatic processing. Pipelines can still override SetPostAssemblyParameters to handle 
	 * this slot manually. 
	 */
	Ignore,

	/** 
	 * The Item's Assembly Output is stored in a property on the Collection Assembly Output, or a 
	 * property within a struct on the Collection Assembly Output.
	 */
	DirectProperty,

	/** 
	 * The Item's Assembly Output is stored in an array on the Collection Assembly Output, or a 
	 * property within a struct in an array on the Collection Assembly Output. 
	 * 
	 * The index into the array should be assigned to PipelineAssemblyOutputArrayIndex on the 
	 * item's FMetaHumanPostAssemblyParameterOutput during assembly.
	 */
	ArrayProperty,

	/** 
	 * GetItemAssemblyOutputReference will be called on the pipeline to fetch a reference to the 
	 * Item's Assembly Output on the Collection Assembly Output.
	 */
	CustomReference,

	/**
	 * GetItemAssemblyOutputValue will be called on the pipeline to fetch the Item's Assembly 
	 * Output by value. 
	 * 
	 * SetItemAssemblyOutputValue will be called to set the Item's Assembly Output after it has 
	 * been modified. 
	 */
	CustomValue
};

/**
 * Features such as Post-Assembly Parameters rely on retrieving an Item's Assembly Output from the
 * Collection Assembly Output.
 * 
 * In many cases, the Item's output is simply stored unmodified on the Collection's output and so 
 * the mapping is very simple. Occasionally the Item's output is stored in a different format on 
 * the Collection's output or transformed in some way and more custom handling is needed.
 * 
 * This struct allows the mapping to be declared, so that simple cases can be handled automatically.
 * This saves pipeline authors from having to write boilerplate code to map one property to another.
 */
USTRUCT()
struct FMetaHumanAssemblyOutputMapping
{
	GENERATED_BODY()

	/** The method used to perform the mapping */
	UPROPERTY()
	EMetaHumanAssemblyOutputMappingMethod Method = EMetaHumanAssemblyOutputMappingMethod::Ignore;

	/** The name of the property on the Collection Assembly Output */
	UPROPERTY()
	FName PipelineOutputPropertyName;

	/**
	 * The name of the property within the struct property specified by PipelineOutputPropertyName.
	 * 
	 * For example, if an Item's Assembly Output is stored within a struct on the Collection 
	 * Assembly Output, so that to access it you would have to write 
	 * CollectionAssemblyOutput.SomeStruct.ItemAssemblyOutput, you would set 
	 * PipelineOutputPropertyName to "SomeStruct" and this property to "ItemAssemblyOutput".
	 */
	UPROPERTY()
	FName PipelineOutputInnerPropertyName;
};

USTRUCT()
struct FMetaHumanCharacterPipelineSlot
{
	GENERATED_BODY()

public:
	UE_API bool IsVirtual() const;

	/**
	 * The type of the PipelineProperties struct stored on FMetaHumanCharacterPaletteItem for 
	 * items in this slot.
	 */
	UPROPERTY()
	TObjectPtr<UScriptStruct> ItemPropertiesStruct;

	UPROPERTY()
	TObjectPtr<UScriptStruct> BuildOutputStruct;

	UPROPERTY()
	TObjectPtr<UScriptStruct> AssemblyInputStruct;

	UPROPERTY()
	TObjectPtr<UScriptStruct> AssemblyOutputStruct;

	// For virtual slots, this is ignored
	UPROPERTY()
	FMetaHumanAssemblyOutputMapping AssemblyOutputMapping;

	/** 
	 * If TargetSlot is a valid name, this slot is a virtual slot that forwards its selections to 
	 * a target slot, which may be virtual or real.
	 */
	UPROPERTY()
	FName TargetSlot;

	/** If true, multiple items can be selected for this slot simultaneously */
	UPROPERTY()
	bool bAllowsMultipleSelection = false;
	
	/** 
	 * If true, this slot will be shown in UI such as the MetaHuman Instance editor.
	 * 
	 * When you have multiple wearables that are processed in the same way by the pipeline, e.g.
	 * earrings and glasses, it's useful to have a single, hidden slot that can process an
	 * arbitrary number of them. You can then easily add virtual slots that target this hidden slot
	 * to allow more types of wearable, without modifying the pipeline.
	 */
	UPROPERTY()
	bool bVisibleToUser = true;

	/** Color used to identify this slot in the UI (e.g. colored band on tile thumbnails) */
	UPROPERTY()
	FLinearColor SlotColor = FLinearColor::White;
};

/**
 * This class represents the data interface of a UMetaHumanCharacterPipeline.
 *
 * It allows code to determine if two pipelines are compatible.
 */
UCLASS(MinimalAPI)
class UMetaHumanCharacterPipelineSpecification : public UObject
{
	GENERATED_BODY()

public:
	UE_API bool IsValid() const;

	/** 
	 * Given a virtual or real slot name, returns the real slot that it resolves to.
	 * 
	 * If the slot name is not found, the return value will be unset.
	 */
	UE_API TOptional<FName> ResolveRealSlotName(FName SlotName) const;

	UPROPERTY()
	TObjectPtr<UScriptStruct> BuildOutputStruct;

	UPROPERTY()
	TObjectPtr<UScriptStruct> AssemblyInputStruct;

	UPROPERTY()
	TObjectPtr<UScriptStruct> AssemblyOutputStruct;

	/**
	 * The specification for each slot.
	 * 
	 * The key is the slot name.
	 */
	UPROPERTY()
	TMap<FName, FMetaHumanCharacterPipelineSlot> Slots;
};

#undef UE_API
