// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanPipelineBuiltDataCollection.h"

FMetaHumanPipelineBuiltDataCollectionEditor FMetaHumanPipelineBuiltDataCollection::Edit()
{
	return FMetaHumanPipelineBuiltDataCollectionEditor(SortedElements);
}

FMetaHumanPipelineBuiltDataCollectionMutableView FMetaHumanPipelineBuiltDataCollection::MutableView()
{
	return FMetaHumanPipelineBuiltDataCollectionMutableView(MakeArrayView(SortedElements));
}

FMetaHumanPipelineBuiltDataCollectionView FMetaHumanPipelineBuiltDataCollection::View() const
{
	return FMetaHumanPipelineBuiltDataCollectionView(MakeConstArrayView(SortedElements));
}

void FMetaHumanPipelineBuiltDataCollection::PostSerialize(const FArchive& Ar)
{
	Edit().PostSerialize(Ar);
}

FMetaHumanPipelineBuiltData& FMetaHumanPipelineBuiltDataCollection::Add(const FMetaHumanPaletteItemPath& ItemPath, FMetaHumanPipelineBuiltData&& Value)
{
	return Edit().Add(ItemPath, MoveTemp(Value));
}

FMetaHumanPipelineBuiltData& FMetaHumanPipelineBuiltDataCollection::Add(const FMetaHumanPaletteItemPath& ItemPath)
{
	return Edit().Add(ItemPath, FMetaHumanPipelineBuiltData());
}

const FMetaHumanPipelineBuiltData& FMetaHumanPipelineBuiltDataCollection::operator[](const FMetaHumanPaletteItemPath& ItemPath) const
{
	return View()[ItemPath];
}

const FMetaHumanPipelineBuiltData* FMetaHumanPipelineBuiltDataCollection::Find(const FMetaHumanPaletteItemPath& ItemPath) const
{
	return View().Find(ItemPath);
}

bool FMetaHumanPipelineBuiltDataCollection::Contains(const FMetaHumanPaletteItemPath& ItemPath) const
{
	return View().Contains(ItemPath);
}
