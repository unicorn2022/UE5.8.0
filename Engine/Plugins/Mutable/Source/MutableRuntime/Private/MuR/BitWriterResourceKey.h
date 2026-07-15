// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Serialization/BitWriter.h"


class FBitWriterResourceKey : public FBitWriter
{
public:
	FBitWriterResourceKey(int64 InMaxBits, bool AllowResize = false);

	virtual FArchive& operator<<(FObjectPtr& Value) override;
};
