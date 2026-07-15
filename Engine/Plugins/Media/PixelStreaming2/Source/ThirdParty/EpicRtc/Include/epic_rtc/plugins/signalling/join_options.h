// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <cstdint>

#pragma pack(push, 8)

enum class EpicRtcSignallingJoinOptions : uint8_t
{
    ResolutionController,
    Simulcast,
    Subscribe,
    P2P,
    Dtx,
    DtlsSrtp,
    SwAec,
    OpenSles,
    PowerMode,
    AudioReconnect,
    DeviceReconnect,
    H264P2P,
    Stereo,
    FailingStatus,
    VideoMaxBitrateP2P,
    ManualAudio,
    Rtx,
    VideoContentType,
    Metal,
    Echo,
    Speaking,
    DominantSpeaker,
    Multistream,
    SyncSubscribe,
    Sctp,
    Internals,
    ReservedAudioStreams,
    Padding,
    TestForceProximityRoom,
    UnifiedPlan,
    RasCsrc,
    SignedAudio,
    SpeakingSelfOnly
};

#pragma pack(pop)