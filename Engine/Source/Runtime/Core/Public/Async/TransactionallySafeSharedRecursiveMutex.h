// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/SharedRecursiveMutex.h"
#include "AutoRTFM.h"

#if UE_AUTORTFM
#include "Async/TransactionallySafeRecursiveMutex.h"
#endif

namespace UE
{

#if UE_AUTORTFM

using FTransactionallySafeSharedRecursiveMutex = ::UE::TTransactionallySafeMutex<::UE::FSharedRecursiveMutex>;

template <>
class TSharedLock<FTransactionallySafeSharedRecursiveMutex> final
{
public:
	TSharedLock(const TSharedLock&) = delete;
	TSharedLock& operator=(const TSharedLock&) = delete;

	[[nodiscard]] inline explicit TSharedLock(FTransactionallySafeSharedRecursiveMutex& Lock)
		: Mutex(Lock)
	{
		Mutex.LockShared(Link);
	}

	inline ~TSharedLock()
	{
		Mutex.UnlockShared(Link);
	}

private:
	FTransactionallySafeSharedRecursiveMutex& Mutex;
	Core::Private::FSharedRecursiveMutexLink Link;
};

#else
using FTransactionallySafeSharedRecursiveMutex = ::UE::FSharedRecursiveMutex;
#endif

}  // namespace UE
