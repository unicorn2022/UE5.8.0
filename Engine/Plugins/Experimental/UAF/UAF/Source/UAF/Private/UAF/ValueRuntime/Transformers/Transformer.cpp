// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAF/ValueRuntime/Transformers/Transformer.h"

namespace UE::UAF
{
	FValueTransformerList::FValueTransformerList() = default;

	FValueTransformerList::FValueTransformerList(FName InTransformerName)
		: TransformerName(InTransformerName)
	{
	}

	FName FValueTransformerList::GetTransformerName() const
	{
		return TransformerName;
	}

	bool FValueTransformerList::AddBoundValueMapTransformer(UScriptStruct* ValueType, FRawTransformerFunc TransformerFunc)
	{
		const int32 EntryIndex = Algo::LowerBound(MakeArrayView(ValueTypes.GetData(), NumBoundValueMapTransformers()), ValueType);
		if (EntryIndex < ValueTypes.Num() && ValueTypes[EntryIndex] == ValueType)
		{
			// A transformer for this value type has already been registered
			return false;
		}

		ValueTypes.Insert(ValueType, EntryIndex);
		Transformers.Insert(TransformerFunc, EntryIndex);
		BoundValueMapTransformerCount++;

		return true;
	}

	bool FValueTransformerList::AddUnboundValueMapTransformer(UScriptStruct* ValueType, FRawTransformerFunc TransformerFunc)
	{
		const int32 NumBoundValueMapEntries = NumBoundValueMapTransformers();
		const int32 NumUnboundValueMapEntries = NumUnboundValueMapTransformers();

		const int32 PerContainerEntryIndex = Algo::LowerBound(MakeArrayView(ValueTypes.GetData() + NumBoundValueMapEntries, NumUnboundValueMapEntries), ValueType);

		// Fixup our index
		const int32 EntryIndex = PerContainerEntryIndex + NumBoundValueMapEntries;

		if (EntryIndex < ValueTypes.Num() && ValueTypes[EntryIndex] == ValueType)
		{
			// A transformer for this value type has already been registered
			return false;
		}

		ValueTypes.Insert(ValueType, EntryIndex);
		Transformers.Insert(TransformerFunc, EntryIndex);

		return true;
	}
}
