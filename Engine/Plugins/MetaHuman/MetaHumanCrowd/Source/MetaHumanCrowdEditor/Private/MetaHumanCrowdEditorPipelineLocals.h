// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Item/MetaHumanCrowdCharacterPipeline.h"
#include "Item/MetaHumanCrowdGroomEditorPipeline.h"
#include "Item/MetaHumanCrowdOutfitEditorPipeline.h"
#include "MetaHumanCharacterPalette.h"
#include "MetaHumanPaletteItemKey.h"
#include "MetaHumanPaletteItemPath.h"
#include "MetaHumanCrowdAnimationConfig.h"

#include "MetaHumanCrowdEditorPipelineLocals.generated.h"

class UMetaHumanCharacter;
class USkeletalMesh;
class UAnimSequence;

/**
 * An item-pipeline build result held temporarily by the collection editor pipeline so that its
 * contents (geometry bundles, hidden face maps, etc.) can be consumed during later build phases,
 * then transformed into the collection-level build output and integrated into the main built data.
 */
USTRUCT()
struct FMetaHumanCrowdDeferredItemBuild
{
	GENERATED_BODY()

public:
	/** The virtual slot that owns this item (passed to IntegrateItemBuiltData later). */
	UPROPERTY()
	FName SlotName;

	/** The raw item-pipeline built data, containing e.g. FMetaHumanCrowdOutfitBuildOutput. */
	UPROPERTY()
	FMetaHumanPaletteBuiltData BuiltData;
};

/**
 * A bake-config entry paired with the merged UAnimSequence it produced.
 */
USTRUCT()
struct FMetaHumanCrowdBakedAnimEntry
{
	GENERATED_BODY()

public:
	/** Source bake config that drove the merge. */
	UPROPERTY()
	FMetaHumanCrowdBakeAnimationData Config;

	/** Merged animation produced for this entry. */
	UPROPERTY()
	TObjectPtr<UAnimSequence> AnimSequence;
};

USTRUCT()
struct FMetaHumanCrowdEditorPipelineLocals
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TMap<FMetaHumanPaletteItemKey, FMetaHumanCrowdOutfitFitTarget> OutfitFitTargets;

	UPROPERTY()
	TMap<FMetaHumanPaletteItemKey, FMetaHumanCrowdGroomFitTarget> HeadGroomFitTargets;

	/** Outfit item pipeline build results, held until transformed into collection-level outputs. */
	UPROPERTY()
	TMap<FMetaHumanPaletteItemPath, FMetaHumanCrowdDeferredItemBuild> DeferredOutfitBuilds;

	/** Groom item pipeline build results, held until transformed into collection-level outputs. */
	UPROPERTY()
	TMap<FMetaHumanPaletteItemPath, FMetaHumanCrowdDeferredItemBuild> DeferredGroomBuilds;

	/**
	 * Head character-pipeline build outputs, held until the collection-level head build output is
	 * synthesised and integrated at the end of the build. Keyed by head item key.
	 */
	UPROPERTY()
	TMap<FMetaHumanPaletteItemKey, FMetaHumanCrowdCharacterBuildOutput> HeadCharacterBuildOutputs;

	/**
	 * Body character-pipeline build outputs, held until the collection-level body build output is
	 * synthesised and integrated at the end of the build. Keyed by body item key.
	 */
	UPROPERTY()
	TMap<FMetaHumanPaletteItemKey, FMetaHumanCrowdCharacterBuildOutput> BodyCharacterBuildOutputs;

	/** Merged animation cache keyed by config entry name. */
	UPROPERTY()
	TMap<FName, FMetaHumanCrowdBakedAnimEntry> AnimSequencePerEntry;

	/** Default pipelines for any items that don't have a pipeline */
	UPROPERTY()
	TMap<FName, TObjectPtr<const UMetaHumanItemPipeline>> DefaultPipelinesByClass;
};
