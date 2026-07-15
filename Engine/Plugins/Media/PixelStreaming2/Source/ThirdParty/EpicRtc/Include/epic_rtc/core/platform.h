// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include <cstdint>

#if defined(ANDROID)
    #include <jni.h>
#endif

#include "epic_rtc/common/memory.h"
#include "epic_rtc/core/callstack_api.h"
#include "epic_rtc/core/conference.h"
#include "epic_rtc/core/conference_config.h"

#pragma pack(push, 8)
class EpicRtcPlatformInterface : public EpicRtcRefCountInterface
{
public:
    EpicRtcPlatformInterface(const EpicRtcPlatformInterface&) = delete;
    EpicRtcPlatformInterface& operator=(const EpicRtcPlatformInterface&) = delete;

    virtual EPICRTC_API EpicRtcErrorCode CreateConference(EpicRtcStringView inId, const EpicRtcConfig& inConfig, EpicRtcConferenceInterface** outConference) = 0;
    virtual EPICRTC_API EpicRtcErrorCode GetConference(EpicRtcStringView inId, EpicRtcConferenceInterface** outConference) const = 0;
    virtual EPICRTC_API void ReleaseConference(EpicRtcStringView inId) = 0;

protected:
    EPICRTC_API EpicRtcPlatformInterface() = default;
    virtual EPICRTC_API ~EpicRtcPlatformInterface() = default;
};

#if defined(ANDROID)
class EpicRtcAndroidPlatformInterface
{
public:
    virtual JavaVM* GetJavaVM() = 0;
    virtual ~EpicRtcAndroidPlatformInterface() = default;
};
#endif

struct EpicRtcPlatformConfig
{
    EpicRtcMemoryInterface* _memory;
    EpicRtcCallstackInterface* _callstack;
#if defined(ANDROID)
    EpicRtcAndroidPlatformInterface* _android;
#endif
};

#if defined(ANDROID)
    static_assert(sizeof(EpicRtcPlatformConfig) == 24);  // Ensure EpicRtcPlatformConfig is expected size on all platforms
#else
    static_assert(sizeof(EpicRtcPlatformConfig) == 16);  // Ensure EpicRtcPlatformConfig is expected size on all platforms
#endif

// Global function for accessing EpicRtcPlatformInferface
extern "C" EPICRTC_API EpicRtcErrorCode GetOrCreatePlatform(const EpicRtcPlatformConfig& inConfig, EpicRtcPlatformInterface** outPlatform);

#pragma pack(pop)
