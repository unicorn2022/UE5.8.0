// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <string>

// epic_rtc
#include "epic_rtc/containers/epic_rtc_string.h"
#include "epic_rtc_helper/memory/ref_count_ptr.h"
#include "epic_rtc_helper/memory/ref_count_impl_helper.h"

namespace EpicRtc
{
    // This is implementation helper for EpicRtcStringInterface.
    // User-code can use this header-based only implementation instead of writing their own bits.
    class StringImpl : public EpicRtcStringInterface
    {
    public:
        inline static EpicRtc::RefCountPtr<StringImpl> Create(const std::string& value)
        {
            return EpicRtc::MakeRefCountPtr<StringImpl>(value);
        }

        const char* Get() const override
        {
            return _data.c_str();
        }

        uint64_t Length() const override
        {
            return _data.length();
        }

    private:
        explicit StringImpl(const std::string& value)
            : _data(value)
        {
        }

        std::string _data;

        EPICRTC_REFCOUNT_INTERFACE_IN_PLACE
    };
} // namespace EpicRtc