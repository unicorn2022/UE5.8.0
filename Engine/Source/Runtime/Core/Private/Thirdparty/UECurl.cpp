// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThirdParty/UECurl.h"

#if WITH_CURL_PLATFORM

#include "CoreGlobals.h"
#include "Containers/Map.h"
#include "HAL/LLM/MemoryUtils.h"
#include "HAL/PlatformStackWalk.h"
#include "HAL/PlatformTLS.h"
#include "Misc/AssertionMacros.h"
#include "Misc/ScopeLock.h"
#include "Misc/StringBuilder.h"

namespace UE::Curl::Private
{

LLM_DEFINE_TAG(Networking_Curl);

/**
* A callback that libcurl will use to allocate memory
*
* @param Size size of allocation in bytes
* @return Pointer to memory chunk or NULL if failed
*/
void* CurlMalloc(size_t Size)
{
	LLM_SCOPE_BYTAG(Networking_Curl);
	return FMemory::Malloc(Size);
}

/**
* A callback that libcurl will use to free memory
*
* @param Ptr pointer to memory chunk (may be NULL)
*/
void CurlFree(void* Ptr)
{
	FMemory::Free(Ptr);
}

/**
* A callback that libcurl will use to reallocate memory
*
* @param Ptr pointer to existing memory chunk (may be NULL)
* @param Size size of allocation in bytes
* @return Pointer to memory chunk or NULL if failed
*/
void* CurlRealloc(void* Ptr, size_t Size)
{
	void* Return = NULL;

	if (Size)
	{
		LLM_SCOPE_BYTAG(Networking_Curl);
		Return = FMemory::Realloc(Ptr, Size);
	}
	else
	{
		FMemory::Free(Ptr);
	}

	return Return;
}

/**
* A callback that libcurl will use to duplicate a string
*
* @param ZeroTerminatedString pointer to string (ANSI or UTF-8, but this does not matter in this case)
* @return Pointer to a copy of string
*/
char* CurlStrdup(const char* ZeroTerminatedString)
{
	char* Copy = NULL;
	check(ZeroTerminatedString);
	if (ZeroTerminatedString)
	{
		LLM_SCOPE_BYTAG(Networking_Curl);

		SIZE_T StrLen = FCStringAnsi::Strlen(ZeroTerminatedString);
		Copy = reinterpret_cast<char*>(FMemory::Malloc(StrLen + 1));
		if (Copy)
		{
			FCStringAnsi::Strncpy(Copy, ZeroTerminatedString, StrLen + 1);
			check(FCStringAnsi::Strcmp(Copy, ZeroTerminatedString) == 0);
		}
	}
	return Copy;
}

/**
* A callback that libcurl will use to allocate zero-initialized memory
*
* @param NumElems number of elements to allocate (may be 0, then NULL should be returned)
* @param ElemSize size of each element in bytes (may be 0)
* @return Pointer to memory chunk, filled with zeroes or NULL if failed
*/
void* CurlCalloc(size_t NumElems, size_t ElemSize)
{
	void* Return = NULL;
	const size_t Size = NumElems * ElemSize;
	if (Size)
	{
		LLM_SCOPE_BYTAG(Networking_Curl);

		Return = FMemory::Malloc(Size);

		if (Return)
		{
			FMemory::Memzero(Return, Size);
		}
	}

	return Return;
}

/** This malloc will init the memory, keeping valgrind happy */
void* CryptoMalloc(size_t Size, const char* File, int Line)
{
	LLM_SCOPE_BYTAG(Networking_Curl);
	void* Result = FMemory::Malloc(Size);
	if (LIKELY(Result))
	{
		FMemory::Memzero(Result, Size);
	}

	return Result;
}

/** This realloc will init the memory, keeping valgrind happy */
void* CryptoRealloc(void* Ptr, const size_t Size, const char* File, int Line)
{
	LLM_SCOPE_BYTAG(Networking_Curl);
	size_t CurrentUsableSize = FMemory::GetAllocSize(Ptr);
	void* Result = FMemory::Realloc(Ptr, Size);
	if (LIKELY(Result) && CurrentUsableSize < Size)
	{
		FMemory::Memzero(reinterpret_cast<uint8*>(Result) + CurrentUsableSize, Size - CurrentUsableSize);
	}

	return Result;
}

void CryptoFree(void* Ptr, const char* File, int Line)
{
	return FMemory::Free(Ptr);
}

#if WITH_CURL_INSTRUMENTATION

void* CurlDisallowMultithreadedLockAllocate()
{
	return new FCurlDisallowMultithreadedLock();
}

void CurlDisallowMultithreadedLockFree(void* In)
{
	if (!In)
	{
		return;
	}

	FCurlDisallowMultithreadedLock* Lock = (FCurlDisallowMultithreadedLock*)In;
	{
		FScopeLock ScopeLock(&Lock->EnteredLock);
		check(Lock->EnteredCount == 0);
	}
	delete Lock;
}

void CurlDisallowMultithreadedLockLock(void* In)
{
	if (!In)
	{
		return;
	}

	FCurlDisallowMultithreadedLock* Lock = (FCurlDisallowMultithreadedLock*)In;

	const uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();

	FScopeLock ScopeLock(&Lock->EnteredLock);

	TArray<uint64> Callstack;
	if (Lock->EnteredCount == 0 || Lock->ThreadOwner != CurrentThreadId)
	{
		Callstack.SetNum(30);
		uint32 NumFrames = FPlatformStackWalk::CaptureStackBackTrace(Callstack.GetData(), Callstack.Num());
		Callstack.SetNum(NumFrames);
	}
	if (Lock->EnteredCount == 0)
	{
		check(Lock->ThreadOwner == FThread::InvalidThreadId);
		Lock->ThreadOwner = CurrentThreadId;
		Lock->OwnerStack = MoveTemp(Callstack);
	}
	else
	{
		if (Lock->ThreadOwner != CurrentThreadId)
		{
			TStringBuilder<2048> Text;
			Text << TEXT("InternalCookerError: Curl TimeTree accessed from two threads at the same time.");
			Text << TEXT("\nCallstack1:");

			FString HumanReadableString;
			for (const uint64 ProgramCounter : Lock->OwnerStack)
			{
				FProgramCounterSymbolInfoEx SymbolInfo;
				FPlatformStackWalk::ProgramCounterToSymbolInfoEx(ProgramCounter, SymbolInfo);
				FPlatformStackWalk::SymbolInfoToHumanReadableStringEx(SymbolInfo, HumanReadableString);

				Text.Append(TEXT("\n\t"));
				Text.Append(HumanReadableString);
				HumanReadableString.Reset();
			}

			Text << TEXT("\nCallstack2:");
			for (const uint64 ProgramCounter : Callstack)
			{
				FProgramCounterSymbolInfoEx SymbolInfo;
				FPlatformStackWalk::ProgramCounterToSymbolInfoEx(ProgramCounter, SymbolInfo);
				FPlatformStackWalk::SymbolInfoToHumanReadableStringEx(SymbolInfo, HumanReadableString);

				Text.Append(TEXT("\n\t"));
				Text.Append(HumanReadableString);
				HumanReadableString.Reset();
			}

			checkf(false, TEXT("%s"), Text.ToString());
		}
	}
	++Lock->EnteredCount;
}

void CurlDisallowMultithreadedLockUnlock(void* In)
{
	if (!In)
	{
		return;
	}

	const uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();

	FCurlDisallowMultithreadedLock* Lock = (FCurlDisallowMultithreadedLock*)In;
	FScopeLock ScopeLock(&Lock->EnteredLock);
	if (Lock->ThreadOwner == CurrentThreadId)
	{
		check(Lock->EnteredCount > 0);
		--Lock->EnteredCount;
		if (Lock->EnteredCount == 0)
		{
			Lock->ThreadOwner = FThread::InvalidThreadId;
			Lock->OwnerStack.Empty();
		}
	}
}

static FCriticalSection GSetTimeTreeLock;
static TMap<void*, void*> GCurlMultiUsedByCurlEasy;
void CurlSetTimeTree(void* Easy, void* Multi, bool bAdded)
{
	FScopeLock SetTimeTreeScopeLock(&GSetTimeTreeLock);
	if (!Multi)
	{
		check(bAdded == false);
		void* ExistingMulti = nullptr;
		GCurlMultiUsedByCurlEasy.RemoveAndCopyValue(Easy, ExistingMulti);
		if (ExistingMulti != nullptr)
		{
			GCurlMultiUsedByCurlEasy.Add(Easy, ExistingMulti);
			FString Message = TEXT("InternalCookerError: a Curl_Easy is being destructed while still in use in the TimeTree for a Curl_Multi.");
			checkf(false, TEXT("%s"), *Message);
		}
	}
	else if (bAdded)
	{
		void*& ExistingMulti = GCurlMultiUsedByCurlEasy.FindOrAdd(Easy, nullptr);
		if (ExistingMulti != nullptr && ExistingMulti != Multi)
		{
			FString Message = TEXT("InternalCookerError: a Curl_Easy is being added to the TimeTree for two Curl_Multis at the same time. Each Curl_Easy is supposed to be used by only one Curl_Multi at once.");
			checkf(false, TEXT("%s"), *Message);
		}
		else
		{
			ExistingMulti = Multi;
		}
	}
	else
	{
		void* ExistingMulti = nullptr;
		GCurlMultiUsedByCurlEasy.RemoveAndCopyValue(Easy, ExistingMulti);
		if (ExistingMulti != Multi && ExistingMulti != nullptr)
		{
			GCurlMultiUsedByCurlEasy.Add(Easy, ExistingMulti);
			FString Message = TEXT("InternalCookerError: a Curl_Easy is being removed from the TimeTree for one Curl_Multi while still in use in the TimeTree for another Curl_Multi. Each Curl_Easy is supposed to be used by only one Curl_Multi at once.");
			checkf(false, TEXT("%s"), *Message);
		}
	}
}

#endif // WITH_CURL_INSTRUMENTATION

} // namespace UE::Curl::Private

#endif // WITH_CURL_PLATFORM