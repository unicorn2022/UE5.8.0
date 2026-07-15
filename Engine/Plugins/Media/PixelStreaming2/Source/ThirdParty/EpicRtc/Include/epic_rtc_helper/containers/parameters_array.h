// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <string>
#include <map>
#include <vector>

// epic_rtc
#include "epic_rtc/containers/epic_rtc_array.h"
#include "epic_rtc_helper/memory/ref_count_ptr.h"
#include "epic_rtc_helper/memory/ref_count_impl_helper.h"

// epic_rtc_helper
#include "epic_rtc_helper/containers/string.h"
#include "epic_rtc_helper/containers/parameter_pair.h"

namespace EpicRtc
{
    // This is implementation helper for EpicRtcParameterPairArrayInterface.
    // User-code can use this header-based only implementation instead of writing their own bits.
    class ParametersArrayImpl : public EpicRtcParameterPairArrayInterface
    {
    public:
        inline static EpicRtc::RefCountPtr<ParametersArrayImpl> Create(const std::map<std::string, std::string>& parameters)
        {
            return EpicRtc::MakeRefCountPtr<ParametersArrayImpl>(parameters);
        }

        // EpicRtcParameterPairArrayInterface implementation
        EpicRtcParameterPairInterface* const* Get() const override
        {
            return _data.data();
        }
        EpicRtcParameterPairInterface** Get() override
        {
            return _data.data();
        }
        uint64_t Size() const override
        {
            return _data.size();
        }

    private:
        explicit ParametersArrayImpl(const std::map<std::string, std::string>& parameters)
        {
            _data.reserve(parameters.size());
            for (auto& [key, value] : parameters)
            {
                _data.push_back(MakeRefCountPtr<ParameterPairImpl>(key, value).Free());
            }
        }

        ParametersArrayImpl() = delete;
        virtual ~ParametersArrayImpl() override
        {
            for (EpicRtcParameterPairInterface*& pair : _data)
            {
                if (pair != nullptr)
                {
                    pair->Release();
                    pair = nullptr;
                }
            }
        }

        std::vector<EpicRtcParameterPairInterface*> _data;

        EPICRTC_REFCOUNT_INTERFACE_IN_PLACE
    };

}  // namespace EpicRtc
