// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#pragma pack(push, 8)

#ifndef EMRTC_STATIC

    #ifdef _WIN32

        #ifdef EMRTC_LIBRARY_IMPL
            #define EPICRTC_API __declspec(dllexport)
        #else
            #define EPICRTC_API __declspec(dllimport)
        #endif

    #else  // _WIN32

        #if __has_attribute(visibility) && defined(EMRTC_LIBRARY_IMPL)
            #define EPICRTC_API __attribute__((visibility("default")))
        #endif

    #endif  // _WIN32

#endif  // EMRTC_STATIC

#ifndef EPICRTC_API
    #define EPICRTC_API
#endif

#pragma pack(pop)

#if !defined(__clang_major__) || __clang_major__ <= 14
    #define EPIC_RTC_STRING_CONSTEXPR
    #define EPIC_RTC_BYTESWAP_CONSTEXPR
    #define epic_rtc_bit_cast _bit_cast
#else
    #define EPIC_RTC_STRING_CONSTEXPR constexpr
    #define EPIC_RTC_BYTESWAP_CONSTEXPR constexpr
    #include <bit>
    #define epic_rtc_bit_cast std::bit_cast
#endif

#include <type_traits>
#include <cstring>

// Implementation from https://en.cppreference.com/w/cpp/numeric/bit_cast.html
template<class To, class From>
std::enable_if_t<
    sizeof(To) == sizeof(From) &&
    std::is_trivially_copyable_v<From> &&
    std::is_trivially_copyable_v<To>,
    To>
_bit_cast(const From& src) noexcept
{
    static_assert(std::is_trivially_constructible_v<To>,
        "This implementation additionally requires "
        "destination type to be trivially constructible");
 
    To dst;
    std::memcpy(&dst, &src, sizeof(To));
    return dst;
}