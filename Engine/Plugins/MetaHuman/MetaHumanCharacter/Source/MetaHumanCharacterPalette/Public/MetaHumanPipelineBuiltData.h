// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanGeneratedAssetMetadata.h"

#include "StructUtils/InstancedStruct.h"
#include "StructUtils/PropertyBag.h"

#include "MetaHumanPipelineBuiltData.generated.h"

/** 
 * The output of a single Character Pipeline's build step.
 * 
 * In other words, the output of the build step for a palette itself, i.e. a Collection or a 
 * Wardrobe Item, not including any items within that palette.
 */
USTRUCT()
struct FMetaHumanPipelineBuiltData
{
	GENERATED_BODY()

public:
	/** 
	 * If this is an item, SlotName is the real slot in the parent pipeline that this item was 
	 * built for.
	 * 
	 * This property is ignored for Collection build output.
	 */
	UPROPERTY()
	FName SlotName;

	UPROPERTY()
	FInstancedStruct BuildOutput;

	/** A struct of parameters that are settable for the assemble step, assigned to their default values */
	UPROPERTY()
	FInstancedPropertyBag AssemblyParameters;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FMetaHumanGeneratedAssetMetadata> Metadata;

	/**
	 * If set, specifies the default sub-directory that this item's assets should be unpacked to
	 * when no explicit metadata entry exists for them.
	 *
	 * Has no influence on assets that have explicit metadata entries.
	 */
	UPROPERTY()
	FString DefaultUnpackSubfolder;

	/** If true, treat DefaultUnpackSubfolder as an absolute package path rather than a relative one. */
	UPROPERTY()
	bool bDefaultSubfolderIsAbsolute = false;
#endif
};
