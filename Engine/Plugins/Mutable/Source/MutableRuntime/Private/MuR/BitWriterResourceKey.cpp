// Copyright Epic Games, Inc. All Rights Reserved.

#include "BitWriterResourceKey.h"

#include "UObject/ObjectPtr.h"


FBitWriterResourceKey::FBitWriterResourceKey(int64 InMaxBits, bool AllowResize): FBitWriter(InMaxBits, AllowResize)
{
}


FArchive& FBitWriterResourceKey::operator<<(FObjectPtr& Value)
{
	Serialize(Value.Get(), sizeof(UObject*));
	return *this;
}
