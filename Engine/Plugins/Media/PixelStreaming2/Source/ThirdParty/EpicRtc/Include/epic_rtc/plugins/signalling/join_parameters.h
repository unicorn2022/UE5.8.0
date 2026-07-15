// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/containers/epic_rtc_array.h"

#pragma pack(push, 8)

struct EpicRtcSignallingJoinParameters
{
    EpicRtcSignallingJoinOptionsArrayInterface* _options;
    uint8_t _reservedAudioStreamsMaxPoolSize;
    EpicRtcBool _audioOnly;
};

static_assert(sizeof(EpicRtcSignallingJoinParameters) == 16);  // Ensure EpicRtcSignallingJoinParameters is expected size on all platforms

#pragma pack(pop)