// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanPaletteItemPath.h"

#include "Misc/NotNull.h"
#include "StructUtils/InstancedStruct.h"

#include "MetaHumanCharacterPaletteItem.generated.h"

struct FMetaHumanPaletteItemKey;
class UMetaHumanCharacterPalette;
class UMetaHumanCharacterPipeline;
class UMetaHumanWardrobeItem;

USTRUCT()
struct FMetaHumanCharacterPaletteItem
{
	GENERATED_BODY()

public:
	/** Return a key for this item that must be unique within its containing palette */
	METAHUMANCHARACTERPALETTE_API FMetaHumanPaletteItemKey GetItemKey() const;

	/** 
	 * Return a friendly name that can be displayed in the UI
	 * 
	 * If DisplayName is set, this just returns DisplayName, otherwise it will use the other 
	 * properties to generate a name.
	 */
	METAHUMANCHARACTERPALETTE_API FText GetOrGenerateDisplayName() const;

#if WITH_EDITORONLY_DATA
	/**
	 * Convenience function for calling LoadSynchronous on the Wardrobe Item's principal asset.
	 * 
	 * Editor only because principal assets are not guaranteed to be cooked, so we shouldn't rely
	 * on loading them outside the editor.
	 */
	METAHUMANCHARACTERPALETTE_API UObject* LoadPrincipalAssetSynchronous() const;
#endif // WITH_EDITORONLY_DATA

	/** 
	 * The Wardrobe Item that this item represents.
	 * 
	 * Note that this could be its own asset or a subobject of a MetaHuman Collection.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Character", meta = (NoResetToDefault))
	TObjectPtr<class UMetaHumanWardrobeItem> WardrobeItem;

	/** A name used to disambiguate items that have the same WardrobeItem */
	UPROPERTY(EditAnywhere, Category = "Character")
	FName Variation;

	/** The slot that this item targets */
	UPROPERTY(VisibleAnywhere, Category = "Character")
	FName SlotName;

#if WITH_EDITORONLY_DATA

	/** An optional display name to use in the editor UI */
	UPROPERTY(EditAnywhere, Category = "Character")
	FText DisplayName;

	/** Editor-only properties managed by the owning palette's pipeline */
	UPROPERTY(EditAnywhere, Category = "Character")
	FInstancedStruct PipelineEditorProperties;

#endif // WITH_EDITORONLY_DATA

	/** 
	 * Properties managed by the owning palette's pipeline.
	 * 
	 * This is useful when a Collection pipeline does significant work on an item that is not
	 * covered by the item's pipeline, and so the Collection needs a place for the user to set
	 * properties to configure this work.
	 */
	UPROPERTY(EditAnywhere, Category = "Character")
	FInstancedStruct PipelineProperties;
};
