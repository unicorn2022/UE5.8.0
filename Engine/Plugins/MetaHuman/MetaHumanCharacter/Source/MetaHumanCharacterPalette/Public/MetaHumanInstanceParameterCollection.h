// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanItemPathMap.h"
#include "MetaHumanPaletteItemPath.h"

#include "StructUtils/PropertyBag.h"

#include "MetaHumanInstanceParameterCollection.generated.h"

#define UE_API METAHUMANCHARACTERPALETTE_API

using FMetaHumanInstanceParameterCollectionEditor = UE::MetaHuman::TMetaHumanItemPathMapEditor<FMetaHumanInstanceParameterCollectionPair, FInstancedPropertyBag>;
using FMetaHumanInstanceParameterCollectionView = UE::MetaHuman::TMetaHumanItemPathMapView<FMetaHumanInstanceParameterCollectionPair, FInstancedPropertyBag>;

USTRUCT()
struct FMetaHumanInstanceParameterCollectionPair
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FMetaHumanPaletteItemPath Key;

	UPROPERTY()
	FInstancedPropertyBag Value;
};

USTRUCT()
struct FMetaHumanInstanceParameterCollection
{
	GENERATED_BODY()

public:
	UE_API FMetaHumanInstanceParameterCollectionEditor Edit();
	UE_API FMetaHumanInstanceParameterCollectionView View() const;

	UE_API void PostSerialize(const FArchive& Ar);

private:
	// Sorted by ItemPath
	UPROPERTY()
	TArray<FMetaHumanInstanceParameterCollectionPair> SortedElements;
};

template<>
struct TStructOpsTypeTraits<FMetaHumanInstanceParameterCollection> : public TStructOpsTypeTraitsBase2<FMetaHumanInstanceParameterCollection>
{
    enum
    {
        WithPostSerialize = true
    };
};

#undef UE_API
