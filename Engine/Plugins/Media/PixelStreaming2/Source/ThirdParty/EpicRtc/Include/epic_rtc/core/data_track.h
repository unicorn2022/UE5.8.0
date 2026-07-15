// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include <cstdint>
#include <cassert>

#include "epic_rtc/common/common.h"
#include "epic_rtc/common/defines.h"

#include "epic_rtc/containers/epic_rtc_string_view.h"
#include "epic_rtc/core/ref_count.h"

#pragma pack(push, 8)

struct EpicRtcDataFrame
{
    /**
     * Data buffer
     * User of the API has ownership of this data and should free once done
     */
    const uint8_t* _data;

    /**
     * Size of the data buffer
     */
    uint32_t _size;

    /**
     * Indicates this is a binary data frame (not a string)
     */
    EpicRtcBool _binary;
};

static_assert(sizeof(EpicRtcDataFrame) == 16);  // Ensure EpicRtcDataFrame is expected size on all platforms

/**
 * Represents the data track. Exposes methods to send and receive custom data.
 */
class EpicRtcDataTrackInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Gets instance Id.
     * @return Id.
     */
    virtual EPICRTC_API EpicRtcStringView GetId() = 0;

    /**
     * Gets instance label.
     * @return Label.
     */
    virtual EPICRTC_API EpicRtcStringView GetLabel() = 0;

    /**
     * Supply frame for processing.
     * @param InFrame Frame to process.
     * @return False if error pushing frame.
     */
    virtual EPICRTC_API EpicRtcBool PushFrame(const EpicRtcDataFrame& inFrame) = 0;

    /**
     * Indicates the track belongs to the remote participant.
     * @return True if the track belongs to the remote participant.
     */
    virtual EPICRTC_API EpicRtcBool IsRemote() = 0;

    /**
     * Gets track state.
     * @return State of the track.
     */
    virtual EPICRTC_API EpicRtcTrackState GetState() = 0;

    // Prevent copying
    EpicRtcDataTrackInterface(const EpicRtcDataTrackInterface&) = delete;
    EpicRtcDataTrackInterface& operator=(const EpicRtcDataTrackInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EPICRTC_API EpicRtcDataTrackInterface() = default;
    virtual EPICRTC_API ~EpicRtcDataTrackInterface() = default;
};

#pragma pack(pop)
