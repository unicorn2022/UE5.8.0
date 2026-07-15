// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

// WITH_CURL_PLATFORM should be enabled on the platforms and configurations that
// support CURL in all or some modules in the executable.
#ifndef WITH_CURL_PLATFORM
#define WITH_CURL_PLATFORM 1
#endif

// WITH_CURL should be enabled in Build.cs files of the modules that use curl
#ifndef WITH_CURL
#define WITH_CURL 0
#endif

// WITH_SSL should be enabled in Build.cs files of the modules that use curl
#ifndef WITH_SSL
#define WITH_SSL 0
#endif

// WITH_CURL_INSTRUMENTATION should be enabled on the platforms which have compiled our
// modified version of curl with extra debug instrumentation, and in configurations in
// which we want that instrumentation active.
#ifndef WITH_CURL_INSTRUMENTATION
#if !defined(PLATFORM_CURL_INCLUDE)
#define WITH_CURL_INSTRUMENTATION WITH_EDITOR && PLATFORM_WINDOWS && !UE_BUILD_SHIPPING
#else
#define WITH_CURL_INSTRUMENTATION 0
#endif
#endif

#if WITH_CURL_PLATFORM

#include "Containers/Array.h"
#include "CoreTypes.h"
#include "HAL/CriticalSection.h"
#include "HAL/Thread.h"
#include "Misc/AssertionMacros.h"
#include "Modules/ModuleManager.h"

#if WITH_CURL

#include "PlatformHttp.h" // Runtime/Online/Http/Public, add module "HTTP" to build.cs
#if WITH_SSL
#include "Interfaces/ISslManager.h" // Runtime/Online/SSL/Public, add module "SSL" to build.cs
#include "SslModule.h" // Runtime/Online/SSL/Public
#endif

#if PLATFORM_MICROSOFT
#include "Microsoft/AllowMicrosoftPlatformTypes.h"
#endif

#ifdef PLATFORM_CURL_INCLUDE
#include PLATFORM_CURL_INCLUDE
#else
#include "curl/curl.h" // Engine/Source/ThirdParty/libcurl, add module "libcurl" to build.cs
#endif

#if PLATFORM_MICROSOFT
#include "Microsoft/HideMicrosoftPlatformTypes.h"
#endif

#if WITH_SSL
#include <openssl/crypto.h> // Engine/Source/ThirdParty/OpenSSL, build.cs dependency on openssl should have been added by libcurl.build.cs
#endif
#endif // WITH_CURL

namespace UE::Curl
{

typedef void* (*CryptoMallocFunc)(size_t Size, const char* File, int Line);
typedef void* (*CryptoReallocFunc)(void* Ptr, const size_t Size, const char* File, int Line);
typedef void (*CryptoFreeFunc)(void* Ptr, const char* File, int Line);

struct FCurlInitializeData
{
	bool bInitialized = false;
	CryptoMallocFunc SavedCryptoMalloc = nullptr;
	CryptoReallocFunc SavedCryptoRealloc = nullptr;
	CryptoFreeFunc SavedCryptoFree = nullptr;
};

} // namespace UE::Curl

namespace UE::Curl::Private
{

#if WITH_CURL_INSTRUMENTATION
/**
 * A lock structure provided to curl that it uses to guard access to data structures from functions that are the curl
 * user's (UnrealEngine's) responsibility to avoid using unsynchronized from multiple threads, e.g. the user must
 * access the functions only within a critical section of its own. This critical section is not expected to be used
 * in release version of the library because it is wastefully expensive. We enable it when debugging our usage of curl.
 * If two threads attempt to enter this critical section at the same time, we log a fatal error.
 */
struct FCurlDisallowMultithreadedLock
{
	FCriticalSection EnteredLock;
	int32 EnteredCount = 0;
	uint32 ThreadOwner = FThread::InvalidThreadId;
	TArray<uint64> OwnerStack;
};

CORE_API void* CurlDisallowMultithreadedLockAllocate();
CORE_API void CurlDisallowMultithreadedLockFree(void* In);
CORE_API void CurlDisallowMultithreadedLockLock(void* In);
CORE_API void CurlDisallowMultithreadedLockUnlock(void* In);
CORE_API void CurlSetTimeTree(void* Easy, void* Multi, bool bAdded);

#endif

CORE_API void* CurlMalloc(size_t Size);
CORE_API void* CurlRealloc(void* Ptr, size_t Size);
CORE_API void CurlFree(void* Ptr);
CORE_API char* CurlStrdup(const char* ZeroTerminatedString);
CORE_API void* CurlCalloc(size_t NumElems, size_t ElemSize);

CORE_API void* CryptoMalloc(size_t Size, const char* File, int Line);
CORE_API void* CryptoRealloc(void* Ptr, const size_t Size, const char* File, int Line);
CORE_API void CryptoFree(void* Ptr, const char* File, int Line);

}

#if WITH_CURL

namespace UE::Curl::Private
{

#if WITH_SSL
inline void SetCryptoMemoryHooks(FCurlInitializeData& InitializeData)
{
	// do not set this in Shipping until we prove that the change in OpenSSL behavior is safe
#if PLATFORM_UNIX && !UE_BUILD_SHIPPING
	CRYPTO_get_mem_functions(&InitializeData.SavedCryptoMalloc, &InitializeData.SavedCryptoRealloc, &InitializeData.SavedCryptoFree);
	CRYPTO_set_mem_functions(CryptoMalloc, CryptoRealloc, CryptoFree);
#endif // PLATFORM_UNIX && !UE_BUILD_SHIPPING
}

inline void UnsetCryptoMemoryHooks(FCurlInitializeData& InitializeData)
{
	// remove our overrides
	// do not set this in Shipping until we prove that the change in OpenSSL behavior is safe
#if PLATFORM_UNIX && !UE_BUILD_SHIPPING
	CRYPTO_set_mem_functions(InitializeData.SavedCryptoMalloc, InitializeData.SavedCryptoRealloc, InitializeData.SavedCryptoFree);
#endif // PLATFORM_UNIX && !UE_BUILD_SHIPPING
	InitializeData.SavedCryptoMalloc = nullptr;
	InitializeData.SavedCryptoRealloc = nullptr;
	InitializeData.SavedCryptoFree = nullptr;
}
#endif // WITH_SSL

// Initialize and ConditionalInitialize must be inline so that each dll calls its own address of
// curl_set_disallowmultithreadedlock. We link curl statically into each dll using it, so every dll has its own copy
// of curl's "global" variables, and each dll needs to set the values of its own copy.
inline int ConditionalInitialize(FCurlInitializeData& InitializeData)
{
	if (InitializeData.bInitialized)
	{
		return 0;
	}
	InitializeData.bInitialized = true;

	int32 CurlInitFlags = CURL_GLOBAL_ALL;
#if WITH_SSL
	// Override libcrypt functions to initialize memory since OpenSSL triggers multiple valgrind warnings due to this.
	// Do this before libcurl/libopenssl/libcrypto has been inited.
	SetCryptoMemoryHooks(InitializeData);

	// Make sure SSL is loaded so that we can use the shared cert pool, and to globally initialize OpenSSL if possible
	FSslModule& SslModule = FModuleManager::LoadModuleChecked<FSslModule>("SSL");
	if (SslModule.GetSslManager().InitializeSsl())
	{
		// Do not need Curl to initialize its own SSL
		CurlInitFlags = CurlInitFlags & ~(CURL_GLOBAL_SSL);
	}
#endif // #if WITH_SSL

	CURLcode InitResult = curl_global_init_mem(CurlInitFlags, CurlMalloc, CurlFree, CurlRealloc, CurlStrdup, CurlCalloc);
#if WITH_CURL_INSTRUMENTATION
	// these functions are only implemented in our custom version of curl
	curl_set_disallowmultithreadedlock(CurlDisallowMultithreadedLockAllocate,
		CurlDisallowMultithreadedLockFree, CurlDisallowMultithreadedLockLock, CurlDisallowMultithreadedLockUnlock);
	curl_set_settimetree(CurlSetTimeTree);
#endif
	return (int)InitResult;
}

inline void ConditionalShutdown(FCurlInitializeData& InitializeData)
{
	if (!InitializeData.bInitialized)
	{
		return;
	}
	InitializeData.bInitialized = false;

	curl_global_cleanup();

	FPlatformHttp::ShutdownPlatformCustomSsl();

#if WITH_SSL
	// Shutdown OpenSSL
	FSslModule& SslModule = FModuleManager::LoadModuleChecked<FSslModule>("SSL");
	SslModule.GetSslManager().ShutdownSsl();

	UnsetCryptoMemoryHooks(InitializeData);
#endif // #if WITH_SSL
}

} // namespace UE::Curl::Private

namespace UE::Curl
{

inline int ConditionalInitialize(FCurlInitializeData& InitializeData)
{
	return UE::Curl::Private::ConditionalInitialize(InitializeData);
}
inline void ConditionalShutdown(FCurlInitializeData& InitializeData)
{
	UE::Curl::Private::ConditionalShutdown(InitializeData);
}

} // namespace UE::Curl

#endif // WITH_CURL

#else // WITH_CURL_PLATFORM

#if WITH_CURL
namespace UE::Curl
{

struct FCurlInitializeData
{

};

inline int ConditionalInitialize(FCurlInitializeData& InitializeData)
{
	return 0;
}
inline void ConditionalShutdown(FCurlInitializeData& InitializeData)
{
}

} // namespace UE::Curl
#endif // WITH_CURL

#endif // WITH_CURL_PLATFORM