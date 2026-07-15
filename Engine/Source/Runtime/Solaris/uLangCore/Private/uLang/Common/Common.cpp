// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Common/Common.h"
#include "uLang/Common/Memory/Allocator.h"

namespace uLang
{

SSystemParams GSystemParams = {};

const CAllocatorInstance GSystemAllocatorInstance(
    [](const CAllocatorInstance *, size_t NumBytes) -> void * { return GSystemParams._HeapMalloc(NumBytes); },
    [](const CAllocatorInstance *, void * Memory, size_t NumBytes) -> void * { return GSystemParams._HeapRealloc(Memory, NumBytes); },
    [](const CAllocatorInstance *, void * Memory) { GSystemParams._HeapFree(Memory); }
);

bool operator==(const SSystemParams& Lhs, const SSystemParams& Rhs)
{
    return Lhs._APIVersion == Rhs._APIVersion &&
        Lhs._HeapMalloc == Rhs._HeapMalloc &&
        Lhs._HeapFree == Rhs._HeapFree &&
        Lhs._HeapRealloc == Rhs._HeapRealloc &&
        Lhs._AssertFailed == Rhs._AssertFailed;
}

EResult Initialize(const SSystemParams & Params)
{
    GSystemParams = Params;

    ULANG_ASSERTF(GSystemParams._APIVersion == ULANG_API_VERSION, "Version mismatch (expected %d, got %d)! Are you linking with a stale DLL?", int32_t(ULANG_API_VERSION), GSystemParams._APIVersion);

    return EResult::OK;
}

bool IsInitialized()
{
    return GSystemParams._APIVersion != 0;
}

EResult DeInitialize()
{
    return EResult::OK;
}

void SetGlobalVerbosity(const uLang::ELogVerbosity GlobalVerbosity)
{
    GSystemParams._Verbosity = GlobalVerbosity;
}

}