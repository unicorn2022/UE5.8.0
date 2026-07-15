// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuR/External/VectorAdapter.h"


namespace UE::Mutable
{
	FVectorAdapter::FVectorAdapter(const FVectorAdapter& Other)
	{
		Value = Other.Value;
	}

	
	FVectorAdapter& FVectorAdapter::operator=(const FVectorAdapter& Other)
	{
		Value = Other.Value;
		return *this;
	}


	FVector4f FVectorAdapter::GetValue() const
	{
		return Value;
	}


	void FVectorAdapter::SetValue(const FVector4f& InValue)
	{
		Value = InValue;
	}
}
