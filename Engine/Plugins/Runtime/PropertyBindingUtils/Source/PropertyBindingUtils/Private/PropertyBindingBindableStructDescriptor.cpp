// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyBindingBindableStructDescriptor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PropertyBindingBindableStructDescriptor)

FPropertyBindingBindableStructDescriptor::~FPropertyBindingBindableStructDescriptor()
{
}

FString FPropertyBindingBindableStructDescriptor::ToString() const
{
	return FString(WriteToString<128>('\'', Name, '\''));
}
