// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCharacterEditorPipelineSpecification.h"
#include "MetaHumanCharacterPaletteItem.h"
#include "MetaHumanCharacterPipeline.h"

#include "Misc/NotNull.h"
#include "UObject/Object.h"

#include "MetaHumanCharacterEditorPipeline.generated.h"

enum class EMetaHumanCharacterPaletteBuildQuality : uint8;
struct FMetaHumanCharacterPaletteItem;
struct FMetaHumanCollectionBuiltData;
class ITargetPlatform;
class UBlueprint;
class UMetaHumanInstance;
class UMetaHumanCharacterPipelineSpecification;
class UMetaHumanCollection;
class UMetaHumanWardrobeItem;

UENUM()
enum class EMetaHumanBuildStatus : uint8
{
	Succeeded,
	Failed
};

UENUM()
enum class EMetaHumanWardrobeItemCompatibility : uint8
{
	/** The Wardrobe Item is not compatible with the slot */
	None,

	/** The Wardrobe Item can be added directly to the slot */
	Add,

	/** The Wardrobe Item can be imported into the slot by calling TryImportWardrobeItem on the Collection Pipeline */
	Import
};

UENUM()
enum class EMetaHumanPipelineDisplayCategory : uint8
{
	Advanced,
	Targets,
	Textures,
	Materials,
};

/**
 * The editor-only component of a UMetaHumanCharacterPipeline.
 */
UCLASS(Abstract, MinimalAPI, NotBlueprintable, EditInlineNew)
class UMetaHumanCharacterEditorPipeline : public UObject
{
	GENERATED_BODY()

public:

	// Metadata tag for properties that should be displayed in the pipeline tool
	inline static const FName PipelineDisplay = TEXT("PipelineDisplay");

#if WITH_EDITOR
	DECLARE_DELEGATE_OneParam(FOnUnpackComplete, EMetaHumanBuildStatus /* Status */);

	using FTryUnpackObjectDelegate = TDelegate<bool(TNotNull<UObject*> /* Object */, FString& /* InOutAssetPackageName */)>;

	/** Get the corresponding runtime pipeline */
	virtual TNotNull<const UMetaHumanCharacterPipeline*> GetRuntimeCharacterPipeline() const
		PURE_VIRTUAL(UMetaHumanCharacterEditorPipeline::GetRuntimeCharacterPipeline, return NewObject<UMetaHumanCharacterPipeline>(););

	/** Returns true if an asset of the given class can be added to the given slot on a palette. */
	METAHUMANCHARACTERPALETTE_API bool IsPrincipalAssetClassCompatibleWithSlot(FName SlotName, TNotNull<const UClass*> AssetClass) const;
	
	/** Tests whether the given Wardrobe Item is compatible with the given slot on a palette. */
	METAHUMANCHARACTERPALETTE_API virtual EMetaHumanWardrobeItemCompatibility TestWardrobeItemCompatibilityWithSlot(FName SlotName, TNotNull<const UMetaHumanWardrobeItem*> WardrobeItem) const;

	UE_DEPRECATED(5.8, "Use TestWardrobeItemCompatibilityWithSlot instead")
	METAHUMANCHARACTERPALETTE_API bool IsWardrobeItemCompatibleWithSlot(FName SlotName, TNotNull<const UMetaHumanWardrobeItem*> WardrobeItem) const;
		
	virtual TNotNull<const UMetaHumanCharacterEditorPipelineSpecification*> GetSpecification() const
		PURE_VIRTUAL(UMetaHumanCharacterEditorPipeline::GetSpecification, return NewObject<UMetaHumanCharacterEditorPipelineSpecification>(););

	/**
	 * @brief Sets the validation context to be used when validating items during assembly. Can be set to nullptr
	 * 
	 * The validation context is ignored unless a pipeline chooses to use it
	 */
	METAHUMANCHARACTERPALETTE_API virtual void SetValidationContext(TScriptInterface<class IMetaHumanValidationContext> InValidationContext) {}

#endif // WITH_EDITOR
};
