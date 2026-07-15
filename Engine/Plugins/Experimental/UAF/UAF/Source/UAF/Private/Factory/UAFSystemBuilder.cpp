// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factory/UAFSystemBuilder.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(UAFSystemBuilder)

uint64 FUAFSystemBuilder::GetKey() const
{
	if (CachedKey == UE::UAF::ISystemBuilder::InvalidKey)
	{
		CachedKey = RecalculateKey();
	}
	return CachedKey;
}
