// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/RecursiveMutex.h"
#include "AutoRTFM.h"

#if UE_AUTORTFM
#include "Async/TransactionallySafeMutexPool.h"
#include "Misc/AssertionMacros.h"
#endif

namespace UE
{

#if UE_AUTORTFM

namespace Private
{

template <typename MutexType, typename... ArgsType>
concept CAllowsLockShared = requires(MutexType& M, ArgsType&&... Args)
{
	M.LockShared(std::forward<ArgsType>(Args)...);
};

template <typename MutexType, typename... ArgsType>
concept CAllowsUnlockShared = requires(MutexType& M, ArgsType&&... Args)
{
	M.UnlockShared(std::forward<ArgsType>(Args)...);
};

template <typename MutexType, typename... ArgsType>
concept CAllowsTryLockShared = requires(MutexType& M, ArgsType&&... Args)
{
	{ M.TryLockShared(std::forward<ArgsType>(Args)...) } -> std::same_as<bool>;
};

template <typename MutexType>
concept CAllowsIsLocked = requires(MutexType& M)
{
	{ M.IsLocked() } -> std::same_as<bool>;
};

template <typename MutexType>
concept CAllowsIsLockShared = requires(MutexType& M)
{
	{ M.IsLockShared() } -> std::same_as<bool>;
};

}  // namespace Private

// A transactionally-safe mutex wrapper that works in the following ways:
// - In the open (non-transactional):
//   - Take the lock like before. Simple!
//   - Free the lock like before too.
// - In the closed (transactional):
//   - During locking we query `TransactionalLockCount`:
//     - 0 means we haven't taken the lock within our transaction nest and need to acquire the lock.
//     - Otherwise we already have the lock (and are preventing non-transactional code seeing any
//       modifications we've made while holding the lock), so just bump `TransactionalLockCount`.
//     - We also register an on-abort handler to release the lock should we abort (but we need to
//       query `TransactionalLockCount` even there because we could be aborting an inner transaction
//       and the parent transaction still wants to have the lock held!).
//   - During unlocking we defer doing the unlock until the transaction commits.
//
// Thus with this approach we will hold this lock for the *entirety* of the transactional nest should
// we take the lock during the transaction, thus preventing non-transactional code from seeing any
// modifications we should make.
//
// If the underlying LockType supports shared locking, shared locks are pessimised to exclusive locks
// within a transaction. Note: it should potentially be possible to have shared locks work correctly,
// but serious care will have to be taken to ensure that we don't have:
//   Open Thread     Closed Thread
//   -----------     SharedLock
//   -----------     SharedUnlock
//   Lock            -------------
//   Unlock          -------------
//   -----------     SharedLock      <- Invalid because the transaction can potentially observe side
//                                      effects of the open-thread's writes!
template <typename LockType>
struct TTransactionallySafeMutex
{
	TTransactionallySafeMutex() : State(UE::Private::TransactionallySafeMutexPool::New<FState>())
	{
	}

	~TTransactionallySafeMutex()
	{
		// Normally, State should never be null, but test "Locked Mutex In Destructed Object" expects
		// that we can memzero a mutex safely. This doesn't crash, but leaks the mutex's State pointer.
		if (State)
		{
			State->ReleaseOnCommit();
			State = nullptr;
		}
	}


	template <typename... ArgsType>
		requires UE::Private::CAllowsLockShared<LockType, ArgsType&&...>
	void LockShared(ArgsType&&... Args)
	{
		if (!AutoRTFM::IsIdle())
		{
			// Transactionally pessimise LockShared -> Lock.
			LockWithinTransaction();
		}
		else
		{
			State->Mutex.LockShared(std::forward<ArgsType>(Args)...);
			ensure(0 == State->TransactionalLockCount);
		}
	}

	template <typename... ArgsType>
		requires UE::Private::CAllowsUnlockShared<LockType, ArgsType&&...>
	void UnlockShared(ArgsType&&... Args)
	{
		if (!AutoRTFM::IsIdle())
		{
			// Transactionally pessimise UnlockShared -> Unlock.
			UnlockWithinTransaction();
		}
		else
		{
			ensure(0 == State->TransactionalLockCount);
			State->Mutex.UnlockShared(std::forward<ArgsType>(Args)...);
		}
	}

	void Lock()
	{
		if (!AutoRTFM::IsIdle())
		{
			LockWithinTransaction();
		}
		else
		{
			State->Mutex.Lock();
			ensure(0 == State->TransactionalLockCount);
		}
	}

	void Unlock()
	{
		if (!AutoRTFM::IsIdle())
		{
			UnlockWithinTransaction();
		}
		else
		{
			ensure(0 == State->TransactionalLockCount);
			State->Mutex.Unlock();
		}
	}

	[[nodiscard]] bool TryLock()
	{
		if (!AutoRTFM::IsIdle())
		{
			return TryLockWithinTransaction();
		}

		checkSlow(0 == State->TransactionalLockCount);
		return State->Mutex.TryLock();
	}

	template <typename... ArgsType>
		requires UE::Private::CAllowsTryLockShared<LockType, ArgsType&&...>
	[[nodiscard]] bool TryLockShared(ArgsType&&... Args)
	{
		if (!AutoRTFM::IsIdle())
		{
			return TryLockWithinTransaction();
		}

		checkSlow(0 == State->TransactionalLockCount);
		return State->Mutex.TryLockShared(std::forward<ArgsType>(Args)...);
	}

	/**
	 * This method may give surprising results and should be used with caution!
	 *
	 * - You cannot safely use this function to determine whether Lock() will block.
	 *   You may be in race with another thread which is also trying to lock the mutex.
	 *
	 * - If a Mutex is locked by the AutoRTFM thread, it will not fully release the mutex
	 *   until the transaction fully succeeds or rolls back. Because of this, IsLocked may
	 *   return true even after the mutex has been fully unlocked.
	 */
	AUTORTFM_OPEN [[nodiscard]] bool IsLocked() const
		requires UE::Private::CAllowsIsLocked<LockType>
	{
 		return State->Mutex.IsLocked();
	}

	/**
	 * This method may give surprising results and should be used with caution!
	 *
	 * - You cannot safely use this function to determine whether LockShared() will block.
	 *   You may be in race with another thread which is also trying to lock the mutex.
	 *
	 * - If a Mutex is locked by the AutoRTFM thread, it will not fully release the mutex
	 *   until the transaction fully succeeds or rolls back. Because of this, IsLockShared may
	 *   return true even after the mutex has been fully unlocked.
	 */
	AUTORTFM_OPEN [[nodiscard]] bool IsLockShared() const
		requires UE::Private::CAllowsIsLockShared<LockType>
	{
		return State->Mutex.IsLockShared();
	}

private:
	UE_NONCOPYABLE(TTransactionallySafeMutex)

	void LockWithinTransaction()
	{
		if (AutoRTFM::IsClosed())
		{
			// We explicitly extend the lifetime of `State` here, in case `this` lives on the
			// stack and ends up being overwritten before the on-abort is hit.
			State->Acquire();

			UE_AUTORTFM_OPEN
			{
				State->LockTransactionalLock();
			};

			UE_AUTORTFM_ONABORT(State = this->State)
			{
				State->UnlockTransactionalLock();
			};

			State->ReleaseOnCommit();
		}
		else
		{
			State->LockTransactionalLock();
		}
	}

	void UnlockWithinTransaction()
	{
		if (AutoRTFM::IsClosed())
		{
			// We explicitly extend the lifetime of `State` here, in case `this` lives on the
			// stack and ends up being overwritten before the on-commit is hit.
			State->Acquire();

			UE_AUTORTFM_ONCOMMIT(State = this->State)
			{
				State->UnlockTransactionalLock();
				State->ReleaseImmediately();
			};
		}
		else
		{
			State->UnlockTransactionalLock();
		}
	}

	[[nodiscard]] bool TryLockWithinTransaction()
	{
		checkSlow(!AutoRTFM::IsIdle());

		// The transactional system which can increment TransactionalLockCount
		// is always single-threaded, thus this is safe to access without atomicity.
		// For TryLock we should only lock when we have a 0 count as no one owns this lock
		bool LockTaken = false;

		if (State->TransactionalLockCount == 0)
		{
			UE_AUTORTFM_OPEN
			{
				if (State->Mutex.TryLock())
				{
					LockTaken = true;
					State->TransactionalLockCount = 1;
				}
			};

			// Only setup the OnAbort if we *did* grab a lock; otherwise, we will not want to do
			// anything with the count or lock.
			if (LockTaken)
			{
				State->Acquire();

				UE_AUTORTFM_ONABORT(State = this->State)
				{
					State->UnlockTransactionalLock();
				};

				State->ReleaseOnCommit();
			}
		}

		return LockTaken;
	}

	struct FState final
	{
		LockType Mutex;
		uint32 TransactionalLockCount = 0;
		uint32 RefCount = 1;

		FState() = default;
		~FState()
		{
			checkSlow(RefCount == 0);
			ensure(0 == TransactionalLockCount);
		}

		AUTORTFM_DISABLE void LockTransactionalLock()
		{
			// The transactional system which can increment TransactionalLockCount
			// is always single-threaded, thus this is safe to check without atomicity.
			if (0 == TransactionalLockCount)
			{
				Mutex.Lock();
			}

			TransactionalLockCount += 1;
		}

		AUTORTFM_DISABLE void UnlockTransactionalLock()
		{
			ensure(TransactionalLockCount > 0);

			TransactionalLockCount -= 1;
			if (TransactionalLockCount == 0)
			{
				Mutex.Unlock();
			}
		}

		AUTORTFM_ENABLE void Acquire()
		{
			RefCount += 1;
		}

		AUTORTFM_ENABLE void ReleaseOnCommit()
		{
			checkSlow(RefCount > 0);

			AutoRTFM::OnCommit([this]
			{
				ReleaseImmediately();
			});
		}

		AUTORTFM_DISABLE void ReleaseImmediately()
		{
			checkSlow(RefCount > 0);

			RefCount -= 1;
			if (RefCount == 0)
			{
				UE::Private::TransactionallySafeMutexPool::Delete(this);
			}
		}
	};

	FState* State = nullptr;
};

using FTransactionallySafeRecursiveMutex = ::UE::TTransactionallySafeMutex<::UE::FRecursiveMutex>;

#else
using FTransactionallySafeRecursiveMutex = ::UE::FRecursiveMutex;
#endif

} // namespace UE
