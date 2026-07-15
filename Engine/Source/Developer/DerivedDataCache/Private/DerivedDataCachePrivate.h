// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

template <typename FuncType> class TUniqueFunction;

namespace UE::DerivedData { class FCacheRecord; }
namespace UE::DerivedData { class IRequestOwner; }

namespace UE::DerivedData::Private
{

// Implemented in DerivedDataCacheModule.cpp
void LaunchTaskInCacheThreadPool(IRequestOwner& Owner, TUniqueFunction<void ()>&& TaskBody);

// Implemented in DerivedDataCacheRecord.cpp
uint64 GetCacheRecordCompressedSize(const FCacheRecord& Record);
uint64 GetCacheRecordTotalRawSize(const FCacheRecord& Record);
uint64 GetCacheRecordRawSize(const FCacheRecord& Record);

} // UE::DerivedData::Private
