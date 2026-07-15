// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanPaletteItemPath.h"

#include "Algo/BinarySearch.h"
#include "Algo/Sort.h"

/**
 * Provides functionality to implement a USTRUCT-based collection with a TMap-like interface that 
 * uses an FMetaHumanPaletteItemPath for a key.
 *
 * The entries are stored in an array sorted by path, so that a view can easily be generated for a
 * given path prefix without allocating memory or copying any data.
 */
namespace UE::MetaHuman
{

template<typename PairType, typename ValueType>
class TMetaHumanItemPathMapView
{
public:
	TMetaHumanItemPathMapView() = default;

	TMetaHumanItemPathMapView(TConstArrayView<PairType> InSortedElements)
		: SortedElements(InSortedElements)
	{
	}

	int32 IndexOf(const FMetaHumanPaletteItemPath& ItemPath) const
	{
		return Algo::BinarySearchBy(SortedElements, ItemPath, &PairType::Key);
	}

	int32 IndexOfChecked(const FMetaHumanPaletteItemPath& ItemPath) const
	{
		const int32 Result = IndexOf(ItemPath);
		check(Result != INDEX_NONE);

		return Result;
	}

	bool Contains(const FMetaHumanPaletteItemPath& ItemPath) const
	{
		return IndexOf(ItemPath) != INDEX_NONE;
	}

	const ValueType* Find(const FMetaHumanPaletteItemPath& ItemPath) const
	{
		const int32 Index = IndexOf(ItemPath);
		if (Index == INDEX_NONE)
		{
			return nullptr;
		}

		return &SortedElements[Index].Value;
	}

	const ValueType& operator[](const FMetaHumanPaletteItemPath& Key) const
	{
		const ValueType* Result = Find(Key);
		check(Result);
		return *Result;
	}

	TMetaHumanItemPathMapView<PairType, ValueType> FilterByBasePath(const FMetaHumanPaletteItemPath& BasePath) const
	{
		int32 StartIndex = INDEX_NONE;
		int32 Count = 0;
		if (!TryFindRangeForBasePath(BasePath, StartIndex, Count))
		{
			// No matching elements, return empty view
			return TConstArrayView<PairType>();
		}
	
		return TMetaHumanItemPathMapView<PairType, ValueType>(SortedElements.Slice(StartIndex, Count));
	}

	bool TryFindRangeForBasePath(
		const FMetaHumanPaletteItemPath& BasePath,
		int32& OutStartIndex, 
		int32& OutCount) const
	{
		OutStartIndex = INDEX_NONE;
		OutCount = 0;

		// Note that shorter paths are considered "less than" longer paths, so any paths prefixed with 
		// BasePath will be greater than BasePath, and hence later in SortedElements.

		// Finds the first element that is >= BasePath, or the end of the array if all elements are < BasePath
		const int32 RangeStartIndex = Algo::LowerBoundBy(SortedElements, BasePath, &PairType::Key);

		if (RangeStartIndex == SortedElements.Num()
			|| !SortedElements[RangeStartIndex].Key.IsEqualOrChildPathOf(BasePath))
		{
			// No elements in range
			return false;
		}

		int32 RangeEndIndex = SortedElements.Num();
		// Find the first element that's not a child of BasePath
		for (int32 Index = RangeStartIndex + 1; Index < SortedElements.Num(); Index++)
		{
			if (!SortedElements[Index].Key.IsEqualOrChildPathOf(BasePath))
			{
				RangeEndIndex = Index;
				break;
			}
		}

		OutStartIndex = RangeStartIndex;
		OutCount = RangeEndIndex - RangeStartIndex;
		return true;
	}

	TConstArrayView<PairType> SortedElements;
};

template<typename PairType, typename ValueType>
class TMetaHumanItemPathMapMutableView : public TMetaHumanItemPathMapView<PairType, ValueType>
{
public:
	using Super = TMetaHumanItemPathMapView<PairType, ValueType>;

	TMetaHumanItemPathMapMutableView() = default;

	TMetaHumanItemPathMapMutableView(TArrayView<PairType> InSortedElements)
		: Super(InSortedElements)
		, MutableSortedElements(InSortedElements)
	{
	}
			
	ValueType* Find(const FMetaHumanPaletteItemPath& ItemPath)
	{
		const int32 Index = Super::IndexOf(ItemPath);
		if (Index == INDEX_NONE)
		{
			return nullptr;
		}

		return &MutableSortedElements[Index].Value;
	}

	ValueType& operator[](const FMetaHumanPaletteItemPath& Key)
	{
		ValueType* Result = Find(Key);
		check(Result);
		return *Result;
	}

	TMetaHumanItemPathMapMutableView<PairType, ValueType> FilterByBasePath(const FMetaHumanPaletteItemPath& BasePath)
	{
		int32 StartIndex = INDEX_NONE;
		int32 Count = 0;
		if (!Super::TryFindRangeForBasePath(BasePath, StartIndex, Count))
		{
			// No matching elements, return empty view
			return TArrayView<PairType>();
		}
	
		return TMetaHumanItemPathMapMutableView<PairType, ValueType>(MutableSortedElements.Slice(StartIndex, Count));
	}

	TArrayView<PairType> MutableSortedElements;
};

template<typename PairType, typename ValueType>
class TMetaHumanItemPathMapEditor
{
public:
	TMetaHumanItemPathMapEditor(TArray<PairType>& InSortedElements)
		: SortedElements(InSortedElements)
	{
	}

	ValueType& Add(const FMetaHumanPaletteItemPath& ItemPath, ValueType&& Value)
	{
		const int32 InsertIndex = Algo::LowerBoundBy(SortedElements, ItemPath, &PairType::Key);
	
		checkf(InsertIndex == SortedElements.Num() || SortedElements[InsertIndex].Key != ItemPath, 
			TEXT("An element with this item path already exists: %s"), *ItemPath.ToDebugString());

		PairType NewPair;
		NewPair.Key = ItemPath;
		NewPair.Value = MoveTemp(Value);
	
		SortedElements.Insert(MoveTemp(NewPair), InsertIndex);
		check(IsSorted());

		return SortedElements[InsertIndex].Value;
	}

	ValueType& Add(const FMetaHumanPaletteItemPath& ItemPath, const ValueType& Value = ValueType())
	{
		ValueType Copy = Value;
		return Add(ItemPath, MoveTemp(Copy));
	}

	ValueType& FindOrAdd(const FMetaHumanPaletteItemPath& ItemPath)
	{
		const int32 Index = View().IndexOf(ItemPath);
		if (Index == INDEX_NONE)
		{
			return Add(ItemPath, ValueType());
		}

		return SortedElements[Index].Value;
	}

	void Append(TMetaHumanItemPathMapEditor<PairType, ValueType>&& SourceEditor)
	{
		TArray<PairType>& SourceSortedElements = SourceEditor.SortedElements;

		if (SourceSortedElements.Num() == 0)
		{
			// No elements to append
			return;
		}

		SortedElements.Reserve(SortedElements.Num() + SourceSortedElements.Num());

		// Use binary search to the find the insert position for the first element of SourceSortedElements
		int32 InsertIndex = Algo::LowerBoundBy(SortedElements, SourceSortedElements[0].Key, &PairType::Key);
	
		checkf(InsertIndex == SortedElements.Num() || SortedElements[InsertIndex].Key != SourceSortedElements[0].Key, 
			TEXT("An element with this item path already exists: %s"), *SourceSortedElements[0].Key.ToDebugString());

		SortedElements.Insert(MoveTemp(SourceSortedElements[0]), InsertIndex);
		InsertIndex++;

		check(IsSorted());

		// Walk through the two arrays in parallel and insert the source elements into this collection
		for (int32 SourceIndex = 1; SourceIndex < SourceSortedElements.Num(); SourceIndex++)
		{
			// Advance InsertIndex to the next element that's higher than the current source element
			while (InsertIndex < SortedElements.Num()
				&& SortedElements[InsertIndex].Key < SourceSortedElements[SourceIndex].Key)
			{
				InsertIndex++;
			}

			checkf(InsertIndex == SortedElements.Num() || SortedElements[InsertIndex].Key != SourceSortedElements[SourceIndex].Key, 
				TEXT("An element with this item path already exists: %s"), *SourceSortedElements[SourceIndex].Key.ToDebugString());

			SortedElements.Insert(MoveTemp(SourceSortedElements[SourceIndex]), InsertIndex);
			InsertIndex++;

			check(IsSorted());
		}
	}

	void RemoveSingleItem(const FMetaHumanPaletteItemPath& ItemPath)
	{
		// Finds the first element that is >= ItemPath, or the end of the array if all elements are < ItemPath
		const int32 GreaterOrEqualItemIndex = Algo::LowerBoundBy(SortedElements, ItemPath, &PairType::Key);

		if (GreaterOrEqualItemIndex < SortedElements.Num()
			&& SortedElements[GreaterOrEqualItemIndex].Key == ItemPath)
		{
			SortedElements.RemoveAt(GreaterOrEqualItemIndex);
		}
	}

	void RemoveItemsByBasePath(const FMetaHumanPaletteItemPath& BasePath)
	{
		int32 StartIndex = INDEX_NONE;
		int32 Count = 0;
		if (View().TryFindRangeForBasePath(BasePath, StartIndex, Count))
		{
			SortedElements.RemoveAt(StartIndex, Count);
		}
	}

	void Empty(int32 Slack = 0)
	{
		SortedElements.Empty(Slack);
	}

	void PostSerialize(const FArchive& Ar)
	{
		if (Ar.IsLoading())
		{
			// Sort order depends on FName sorting, which is not guaranteed to be the same across 
			// different engine instances, so we need to re-sort after loading.
			//
			// Note that this means the SortedElements will be saved in a non-deterministic order, 
			// which may cause problems with cook determinism. It's recommended not to store this type
			// of collection in a property that will be saved to disk and use it for transient 
			// properties only.
			Algo::SortBy(SortedElements, &PairType::Key);
		}
	}

	TMetaHumanItemPathMapView<PairType, ValueType> View() const
	{
		return TMetaHumanItemPathMapView<PairType, ValueType>(MakeConstArrayView(SortedElements));
	}

private:
	bool IsSorted() const
	{
		for (int32 Index = 1; Index < SortedElements.Num(); Index++)
		{
			if (!(SortedElements[Index - 1].Key < SortedElements[Index].Key))
			{
				return false;
			}
		}

		return true;
	}

	TArray<PairType>& SortedElements;
};

} // namespace UE::MetaHuman 
