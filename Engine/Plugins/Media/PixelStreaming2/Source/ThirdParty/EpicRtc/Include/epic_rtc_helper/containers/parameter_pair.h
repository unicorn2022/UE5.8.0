// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <string>

// epic_rtc
#include "epic_rtc/containers/epic_rtc_string.h"
#include "epic_rtc_helper/memory/ref_count_ptr.h"
#include "epic_rtc_helper/memory/ref_count_impl_helper.h"

// epic_rtc_helper
#include "epic_rtc_helper/containers/string.h"

namespace EpicRtc
{
    // This is implementation helper for EpicRtcParameterPairInterface.
    // User-code can use this header-based only implementation instead of writing their own bits.
    class ParameterPairImpl : public EpicRtcParameterPairInterface
    {
    public:
        inline static EpicRtc::RefCountPtr<ParameterPairImpl> Create(const std::string& key, const std::string& value)
        {
            return EpicRtc::MakeRefCountPtr<ParameterPairImpl>(key, value);
        }

        // EpicRtcParameterPairInterface implementation
        EpicRtcStringInterface* GetKey() override
        {
            return _key;
        }

        EpicRtcStringInterface* GetValue() override
        {
            return _value;
        }

    private:
        explicit ParameterPairImpl(const std::string& key, const std::string& value)
            : _key(MakeRefCountPtr<StringImpl>(key).Free())
            , _value(MakeRefCountPtr<StringImpl>(value).Free())
        {
        }

        ParameterPairImpl() = delete;
        virtual ~ParameterPairImpl() override
        {
            if (_key != nullptr)
            {
                _key->Release();
                _key = nullptr;
            }
            
            if (_value != nullptr)
            {
                _value->Release();
                _value = nullptr;
            }
        }

        EpicRtcStringInterface* _key;
        EpicRtcStringInterface* _value;

        EPICRTC_REFCOUNT_INTERFACE_IN_PLACE
    };
} // namespace EpicRtc