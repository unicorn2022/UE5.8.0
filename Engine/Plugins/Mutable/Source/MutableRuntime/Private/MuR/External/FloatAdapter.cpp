// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuR/External/FloatAdapter.h"


namespace UE::Mutable
{
	FFloatAdapter::FFloatAdapter(const FFloatAdapter& Other)
	{
		Value = Other.Value;
	}

	
	FFloatAdapter& FFloatAdapter::operator=(const FFloatAdapter& Other)
	{
		Value = Other.Value;
		return *this;
	}


	float FFloatAdapter::GetValue() const
	{
		return Value;
	}


	void FFloatAdapter::SetValue(float InValue)
	{
		Value = InValue;
	}
}
