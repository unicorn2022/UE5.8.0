// Copyright Epic Games, Inc. All Rights Reserved.

#include "Async/AtomicRefCount.h"
#include "Misc/AssertionMacros.h"

void UE::Private::CheckRefCountIsNonZero()
{
	check(!"Release() was called on an object which is already at a zero refcount.");
}
