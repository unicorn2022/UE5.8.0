// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanItemPathMap.h"
#include "MetaHumanPaletteItemPath.h"
#include "MetaHumanPostAssemblyParameterOutput.h"

#include "StructUtils/PropertyBag.h"

#include "MetaHumanPostAssemblyParameterOutputCollection.generated.h"

#define UE_API METAHUMANCHARACTERPALETTE_API

using FMetaHumanPostAssemblyParameterOutputCollectionEditor = UE::MetaHuman::TMetaHumanItemPathMapEditor<FMetaHumanPostAssemblyParameterOutputCollectionPair, FMetaHumanPostAssemblyParameterOutput>;
using FMetaHumanPostAssemblyParameterOutputCollectionView = UE::MetaHuman::TMetaHumanItemPathMapView<FMetaHumanPostAssemblyParameterOutputCollectionPair, FMetaHumanPostAssemblyParameterOutput>;

USTRUCT()
struct FMetaHumanPostAssemblyParameterOutputCollectionPair
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FMetaHumanPaletteItemPath Key;

	UPROPERTY()
	FMetaHumanPostAssemblyParameterOutput Value;
};

USTRUCT()
struct FMetaHumanPostAssemblyParameterOutputCollection
{
	GENERATED_BODY()

public:
	UE_API FMetaHumanPostAssemblyParameterOutputCollectionEditor Edit();
	UE_API FMetaHumanPostAssemblyParameterOutputCollectionView View() const;

	UE_API void PostSerialize(const FArchive& Ar);

private:
	// Sorted by ItemPath
	UPROPERTY()
	TArray<FMetaHumanPostAssemblyParameterOutputCollectionPair> SortedElements;
};

template<>
struct TStructOpsTypeTraits<FMetaHumanPostAssemblyParameterOutputCollection> : public TStructOpsTypeTraitsBase2<FMetaHumanPostAssemblyParameterOutputCollection>
{
    enum
    {
        WithPostSerialize = true
    };
};

#undef UE_API
