// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanItemPathMap.h"
#include "MetaHumanPipelineBuiltData.h"

#include "MetaHumanPipelineBuiltDataCollection.generated.h"

#define UE_API METAHUMANCHARACTERPALETTE_API

using FMetaHumanPipelineBuiltDataCollectionEditor = UE::MetaHuman::TMetaHumanItemPathMapEditor<FMetaHumanPipelineBuiltDataCollectionPair, FMetaHumanPipelineBuiltData>;
using FMetaHumanPipelineBuiltDataCollectionView = UE::MetaHuman::TMetaHumanItemPathMapView<FMetaHumanPipelineBuiltDataCollectionPair, FMetaHumanPipelineBuiltData>;
using FMetaHumanPipelineBuiltDataCollectionMutableView = UE::MetaHuman::TMetaHumanItemPathMapMutableView<FMetaHumanPipelineBuiltDataCollectionPair, FMetaHumanPipelineBuiltData>;

USTRUCT()
struct FMetaHumanPipelineBuiltDataCollectionPair
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FMetaHumanPaletteItemPath Key;

	UPROPERTY()
	FMetaHumanPipelineBuiltData Value;
};

USTRUCT()
struct FMetaHumanPipelineBuiltDataCollection
{
	GENERATED_BODY()

public:
	UE_API FMetaHumanPipelineBuiltDataCollectionEditor Edit();
	UE_API FMetaHumanPipelineBuiltDataCollectionMutableView MutableView();
	UE_API FMetaHumanPipelineBuiltDataCollectionView View() const;

	UE_API void PostSerialize(const FArchive& Ar);

	// Cover the most commonly used functions from when this was a TMap

	UE_DEPRECATED(5.8, "Call Edit() to get an editing interface to this collection")
	UE_API FMetaHumanPipelineBuiltData& Add(const FMetaHumanPaletteItemPath& ItemPath, FMetaHumanPipelineBuiltData&& Value);

	UE_DEPRECATED(5.8, "Call Edit() to get an editing interface to this collection")
	UE_API FMetaHumanPipelineBuiltData& Add(const FMetaHumanPaletteItemPath& ItemPath);

	UE_DEPRECATED(5.8, "Call View() to get a read-only view onto this collection")
	UE_API const FMetaHumanPipelineBuiltData& operator[](const FMetaHumanPaletteItemPath& ItemPath) const;

	UE_DEPRECATED(5.8, "Call View() to get a read-only view onto this collection")
	UE_API const FMetaHumanPipelineBuiltData* Find(const FMetaHumanPaletteItemPath& ItemPath) const;

	UE_DEPRECATED(5.8, "Call View() to get a read-only view onto this collection")
	UE_API bool Contains(const FMetaHumanPaletteItemPath& ItemPath) const;

private:
	// Sorted by ItemPath
	UPROPERTY()
	TArray<FMetaHumanPipelineBuiltDataCollectionPair> SortedElements;
};

template<>
struct TStructOpsTypeTraits<FMetaHumanPipelineBuiltDataCollection> : public TStructOpsTypeTraitsBase2<FMetaHumanPipelineBuiltDataCollection>
{
    enum
    {
        WithPostSerialize = true
    };
};

#undef UE_API
