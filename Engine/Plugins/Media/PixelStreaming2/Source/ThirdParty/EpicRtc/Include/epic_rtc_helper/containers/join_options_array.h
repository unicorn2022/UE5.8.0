// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <vector>

// epic_rtc
#include "epic_rtc/containers/epic_rtc_array.h"

// epic_rtc_helper
#include "epic_rtc_helper/memory/ref_count_ptr.h"
#include "epic_rtc_helper/memory/ref_count_impl_helper.h"

namespace EpicRtc
{
    // This is implementation helper for EpicRtcSignallingJoinOptionsArrayInterface.
    class JoinOptionsArrayImpl : public EpicRtcSignallingJoinOptionsArrayInterface
    {
    public:
        inline static EpicRtc::RefCountPtr<JoinOptionsArrayImpl> Create(const std::vector<EpicRtcSignallingJoinOptions>& joinOptions)
        {
            return EpicRtc::MakeRefCountPtr<JoinOptionsArrayImpl>(joinOptions);
        }

        const EpicRtcSignallingJoinOptions* Get() const override
        {
            return _data.data();
        }
        EpicRtcSignallingJoinOptions* Get() override
        {
            return _data.data();
        }
        uint64_t Size() const override
        {
            return _data.size();
        }

    private:
        explicit JoinOptionsArrayImpl(const std::vector<EpicRtcSignallingJoinOptions>& joinOptions)
            : _data(joinOptions)
        {
        }

        JoinOptionsArrayImpl() = delete;
        virtual ~JoinOptionsArrayImpl() = default;

        std::vector<EpicRtcSignallingJoinOptions> _data;

        EPICRTC_REFCOUNT_INTERFACE_IN_PLACE
    };

}  // namespace EpicRtc
