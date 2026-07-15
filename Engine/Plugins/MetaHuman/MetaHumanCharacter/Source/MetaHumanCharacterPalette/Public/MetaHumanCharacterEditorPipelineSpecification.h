// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/NotNull.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPtr.h"

#include "MetaHumanCharacterEditorPipelineSpecification.generated.h"

class UMetaHumanCharacterPipelineSpecification;
struct FAssetData;

#define UE_API METAHUMANCHARACTERPALETTE_API

USTRUCT()
struct FMetaHumanCharacterPipelineSlotEditorData
{
	GENERATED_BODY()

	/** Returns true if the given asset is accepted by this slot */
	UE_API bool SupportsAsset(const FAssetData& Asset) const;

	/** Returns true if the given asset class is supported by the slot */
	UE_API bool SupportsAssetType(TNotNull<const UClass*> AssetType) const;

	/** The type of the expected Build Input struct for this slot */
	UPROPERTY()
	TObjectPtr<UScriptStruct> BuildInputStruct;

	/**
	 * The type of the PipelineEditorProperties struct stored on FMetaHumanCharacterPaletteItem for 
	 * items in this slot.
	 */
	UPROPERTY()
	TObjectPtr<UScriptStruct> ItemEditorPropertiesStruct;

	/**
	 * The asset types accepted by this slot.
	 *
	 * If the runtime slot is a virtual slot, SupportedPrincipalAssetTypes on this slot must be a 
	 * subset of the target slot's SupportedPrincipalAssetTypes.
	 */
	UPROPERTY()
	TArray<TSoftClassPtr<UObject>> SupportedPrincipalAssetTypes;
};


UCLASS(MinimalAPI)
class UMetaHumanCharacterEditorPipelineSpecification : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Returns true if the editor specification is internally consistent and consistent with the
	 * given runtime specification.
	 */
	UE_API bool IsValid(TNotNull<const UMetaHumanCharacterPipelineSpecification*> RuntimeSpec) const;

	/**
	 * The type of the expected Build Input struct for this palette.
	 * 
	 * This is on the editor specification, rather than the runtime specification, so that it can
	 * reference editor-only types.
	 */
	UPROPERTY()
	TObjectPtr<UScriptStruct> BuildInputStruct;

	/** 
	 * Editor-only data for slots defined in the runtime pipeline spec.
	 * 
	 * Slots in the runtime pipeline spec, i.e. UMetaHumanCharacterPipelineSpecification, may or 
	 * may not have editor-only data here.
	 * 
	 * The runtime pipeline spec is the source of truth for what slots exist on the pipeline. 
	 * SlotEditorData is not guaranteed to contain all slots, so when iterating slots, for example,
	 * use the runtime pipeline spec instead and use SlotEditorData only as a lookup.
	 */
	UPROPERTY()
	TMap<FName, FMetaHumanCharacterPipelineSlotEditorData> SlotEditorData;
};

#undef UE_API
