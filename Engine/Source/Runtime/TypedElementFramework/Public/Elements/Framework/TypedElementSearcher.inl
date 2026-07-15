// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageInterface.h"

namespace UE::Editor::DataStorage
{
	template<typename ColumnType>
	void TColumnSearcherInterface<ColumnType>::GetSearchableString(
		FString& SearchableString, const ICoreProvider& Storage, RowHandle Row) const
	{
		if (const ColumnType* Column = Storage.GetColumn<ColumnType>(Row))
		{
			GetSearchableString(SearchableString, Storage, *Column);
		}
	}
}// namespace UE::Editor::DataStorage