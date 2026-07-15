// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanInstanceParameterCollection.h"

FMetaHumanInstanceParameterCollectionEditor FMetaHumanInstanceParameterCollection::Edit()
{
	return FMetaHumanInstanceParameterCollectionEditor(SortedElements);
}

FMetaHumanInstanceParameterCollectionView FMetaHumanInstanceParameterCollection::View() const
{
	return FMetaHumanInstanceParameterCollectionView(MakeConstArrayView(SortedElements));
}

void FMetaHumanInstanceParameterCollection::PostSerialize(const FArchive& Ar)
{
	Edit().PostSerialize(Ar);
}
