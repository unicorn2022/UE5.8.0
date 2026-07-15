// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#define UE_TCOMPACT_SET_FORCE_ARRAY_VIEW // always enabled for explicitly typed compact sets
#define UE_TCOMPACT_SET TCompactSet
#include "CompactSet.h.inl"
#undef UE_TCOMPACT_SET
#undef UE_TCOMPACT_SET_FORCE_ARRAY_VIEW