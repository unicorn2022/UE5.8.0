// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanGeneratedAssetMetadata.generated.h"

#define UE_API METAHUMANCHARACTERPALETTE_API

/**
 * Metadata about a generated asset, usually one that is not in its own package.
 * 
 * This is used when unpacking assets into their own packages, to give them friendly names and
 * helpful paths chosen by the system that generated them.
 */
USTRUCT()
struct FMetaHumanGeneratedAssetMetadata
{
	GENERATED_BODY()

	FMetaHumanGeneratedAssetMetadata() = default;
	~FMetaHumanGeneratedAssetMetadata() = default;

	FMetaHumanGeneratedAssetMetadata(const FMetaHumanGeneratedAssetMetadata&) = default;
	FMetaHumanGeneratedAssetMetadata(FMetaHumanGeneratedAssetMetadata&&) = default;
	FMetaHumanGeneratedAssetMetadata& operator=(const FMetaHumanGeneratedAssetMetadata&) = default;
	FMetaHumanGeneratedAssetMetadata& operator=(FMetaHumanGeneratedAssetMetadata&&) = default;

	UE_API FMetaHumanGeneratedAssetMetadata(TObjectPtr<UObject> InObject, const FString& InPreferredSubfolderPath, const FString& InPreferredName, bool bInSubfolderIsAbsolute = false);

	UPROPERTY()
	TObjectPtr<UObject> Object;

	/**
	 * A hint providing a useful subfolder path that this asset could be unpacked to.
	 *
	 * May contain multiple path elements, e.g. "Face/Textures".
	 */
	UPROPERTY()
	FString PreferredSubfolderPath;

	/** If true, treat PreferredSubfolderPath as an absolute package path. */
	UPROPERTY()
	bool bSubfolderIsAbsolute = false;

	/** A hint providing a useful name that this asset could be given when it's unpacked. */
	UPROPERTY()
	FString PreferredName;
};

#undef UE_API
