// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanPostAssemblyParameterOutputCollection.h"

FMetaHumanPostAssemblyParameterOutputCollectionEditor FMetaHumanPostAssemblyParameterOutputCollection::Edit()
{
	return FMetaHumanPostAssemblyParameterOutputCollectionEditor(SortedElements);
}

FMetaHumanPostAssemblyParameterOutputCollectionView FMetaHumanPostAssemblyParameterOutputCollection::View() const
{
	return FMetaHumanPostAssemblyParameterOutputCollectionView(MakeConstArrayView(SortedElements));
}

void FMetaHumanPostAssemblyParameterOutputCollection::PostSerialize(const FArchive& Ar)
{
	Edit().PostSerialize(Ar);
}
