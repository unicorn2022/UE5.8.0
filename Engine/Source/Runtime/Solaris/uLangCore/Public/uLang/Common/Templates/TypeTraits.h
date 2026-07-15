// Copyright Epic Games, Inc. All Rights Reserved.
// Templates for determining properties/traits of types

#pragma once

#include "uLang/Common/Common.h"
#include <initializer_list>
#include <type_traits>

namespace uLang
{

//------------------------------------------------------------------
// From IsConstructible.h

/**
 * Determines if T is constructible from a set of arguments.
 */
template <typename T, typename... Args>
struct TIsConstructible
{
    static constexpr bool Value = __is_constructible(T, Args...);
};

//------------------------------------------------------------------
// From IsContiguousContainer.h

/**
 * Traits class which tests if a type is a contiguous container.
 * Requires:
 *    [ &Container[0], &Container[0] + Num ) is a valid range
 */
template <typename T>
struct TIsContiguousContainer
{
    static constexpr bool Value = false;
};

template <typename T> struct TIsContiguousContainer<             T& > : TIsContiguousContainer<T> {};
template <typename T> struct TIsContiguousContainer<             T&&> : TIsContiguousContainer<T> {};
template <typename T> struct TIsContiguousContainer<const          T> : TIsContiguousContainer<T> {};
template <typename T> struct TIsContiguousContainer<      volatile T> : TIsContiguousContainer<T> {};
template <typename T> struct TIsContiguousContainer<const volatile T> : TIsContiguousContainer<T> {};

/**
 * Specialization for C arrays (always contiguous)
 */
template <typename T, size_t N> struct TIsContiguousContainer<               T[N]> { static constexpr bool Value = true; };
template <typename T, size_t N> struct TIsContiguousContainer<const          T[N]> { static constexpr bool Value = true; };
template <typename T, size_t N> struct TIsContiguousContainer<      volatile T[N]> { static constexpr bool Value = true; };
template <typename T, size_t N> struct TIsContiguousContainer<const volatile T[N]> { static constexpr bool Value = true; };

/**
 * Specialization for initializer lists (also always contiguous)
 */
template <typename T>
struct TIsContiguousContainer<std::initializer_list<T>>
{
    static constexpr bool Value = true;
};

//------------------------------------------------------------------
// From UnrealTypeTraits.h

/**
 * TIsZeroConstructType
 */
template<typename T>
struct TIsZeroConstructType
{
    static constexpr bool Value = std::is_enum_v<T> || std::is_arithmetic_v<T> || std::is_pointer_v<T>;
};

/*-----------------------------------------------------------------------------
    Call traits - Modeled somewhat after boost's interfaces.
-----------------------------------------------------------------------------*/

/**
 * Call traits helpers
 */
template <typename T, bool TypeIsSmall>
struct TCallTraitsParamTypeHelper
{
    using ParamType = const T&;
    using ConstParamType = const T&;
};
template <typename T>
struct TCallTraitsParamTypeHelper<T, true>
{
    using ParamType = const T;
    using ConstParamType = const T;
};
template <typename T>
struct TCallTraitsParamTypeHelper<T*, true>
{
    using ParamType = T*;
    using ConstParamType = const T*;
};

/*-----------------------------------------------------------------------------
 * TCallTraits
 *
 * Same call traits as boost, though not with as complete a solution.
 *
 * The main member to note is ParamType, which specifies the optimal
 * form to pass the type as a parameter to a function.
 *
 * Has a small-value optimization when a type is a POD type and as small as a pointer.
-----------------------------------------------------------------------------*/

/**
 * base class for call traits. Used to more easily refine portions when specializing
 */
template <typename T>
struct TCallTraitsBase
{
private:
    static constexpr bool PassByValue = ((sizeof(T) <= sizeof(void*)) && std::is_trivial_v<T> && std::is_standard_layout_v<T>) || std::is_arithmetic_v<T> || std::is_pointer_v<T>;

public:
    using ValueType = T;
    using Reference = T&;
    using ConstReference = const T&;
    using ParamType = typename TCallTraitsParamTypeHelper<T, PassByValue>::ParamType;
    using ConstPointerType = typename TCallTraitsParamTypeHelper<T, PassByValue>::ConstParamType;
};

/**
 * TCallTraits
 */
template <typename T>
struct TCallTraits : public TCallTraitsBase<T> {};

// Fix reference-to-reference problems.
template <typename T>
struct TCallTraits<T&>
{
    using ValueType = T&;
    using Reference = T&;
    using ConstReference = const T&;
    using ParamType = T&;
    using ConstPointerType = T&;
};

// Array types
template <typename T, size_t N>
struct TCallTraits<T [N]>
{
private:
    using ArrayType = T[N];
public:
    using ValueType = const T*;
    using Reference = ArrayType&;
    using ConstReference = const ArrayType&;
    using ParamType = const T* const;
    using ConstPointerType = const T* const;
};

// const array types
template <typename T, size_t N>
struct TCallTraits<const T [N]>
{
private:
    using ArrayType = T[N];
public:
    using ValueType = const T*;
    using Reference = ArrayType&;
    using ConstReference = const ArrayType&;
    using ParamType = const T* const;
    using ConstPointerType = const T* const;
};

/**
 * Helper for array traits. Provides a common base to more easily refine a portion of the traits
 * when specializing. Mainly used by MemoryOps.h which is used by the contiguous storage containers like TArray.
 */
template<typename T>
struct TTypeTraitsBase
{
    using ConstInitType = typename TCallTraits<T>::ParamType;
    using ConstPointerType = typename TCallTraits<T>::ConstPointerType;

    // There's no good way of detecting this so we'll just assume it to be true for certain known types and expect
    // users to customize it for their custom types.
    static constexpr bool IsBytewiseComparable = std::is_enum_v<T> || std::is_arithmetic_v<T> || std::is_pointer_v<T>;
};

/**
 * Traits for types.
 */
template<typename T> struct TTypeTraits : public TTypeTraitsBase<T> {};

/**
 * Traits for containers.
 */
template<typename T> struct TContainerTraitsBase
{
    // This should be overridden by every container that supports emptying its contents via a move operation.
    static constexpr bool MoveWillEmptyContainer = false;
};

template<typename T> struct TContainerTraits : public TContainerTraitsBase<T> {};

/**
 * Tests if a type T is bitwise-constructible from a given argument type U.  That is, whether or not
 * the U can be memcpy'd in order to produce an instance of T, rather than having to go
 * via a constructor.
 *
 * Examples:
 * TIsBitwiseConstructible<PODType,    PODType   >::Value == true  // PODs can be trivially copied
 * TIsBitwiseConstructible<const int*, int*      >::Value == true  // a non-const Derived pointer is trivially copyable as a const Base pointer
 * TIsBitwiseConstructible<int*,       const int*>::Value == false // not legal the other way because it would be a const-correctness violation
 * TIsBitwiseConstructible<int32_t,    uint32_t  >::Value == true  // signed integers can be memcpy'd as unsigned integers
 * TIsBitwiseConstructible<uint32_t,   int32_t   >::Value == true  // and vice versa
 */

template <typename T, typename Arg>
struct TIsBitwiseConstructible
{
    static_assert(
        !std::is_reference_v<T  > &&
        !std::is_reference_v<Arg>,
        "TIsBitwiseConstructible is not designed to accept reference types");

    static_assert(
        std::is_same_v<T,   typename std::remove_cv_t<T  >> &&
        std::is_same_v<Arg, typename std::remove_cv_t<Arg>>,
        "TIsBitwiseConstructible is not designed to accept qualified types");

    // Assume no bitwise construction in general
    static constexpr bool Value = false;
};

template <typename T>
struct TIsBitwiseConstructible<T, T>
{
    // Ts can always be bitwise constructed from itself if it is trivially copyable.
    static constexpr bool Value = std::is_trivially_copy_constructible_v<T>;
};

template <typename T, typename U>
struct TIsBitwiseConstructible<const T, U> : TIsBitwiseConstructible<T, U>
{
    // Constructing a const T is the same as constructing a T
};

// Const pointers can be bitwise constructed from non-const pointers.
// This is not true for pointer conversions in general, e.g. where an offset may need to be applied in the case
// of multiple inheritance, but there is no way of detecting that at compile-time.
template <typename T>
struct TIsBitwiseConstructible<const T*, T*>
{
    // Constructing a const T is the same as constructing a T
    static constexpr bool Value = true;
};

// Unsigned types can be bitwise converted to their signed equivalents, and vice versa.
// (assuming two's-complement, which we are)
template <> struct TIsBitwiseConstructible< uint8_t,   int8_t> { static constexpr bool Value = true; };
template <> struct TIsBitwiseConstructible<  int8_t,  uint8_t> { static constexpr bool Value = true; };
template <> struct TIsBitwiseConstructible<uint16_t,  int16_t> { static constexpr bool Value = true; };
template <> struct TIsBitwiseConstructible< int16_t, uint16_t> { static constexpr bool Value = true; };
template <> struct TIsBitwiseConstructible<uint32_t,  int32_t> { static constexpr bool Value = true; };
template <> struct TIsBitwiseConstructible< int32_t, uint32_t> { static constexpr bool Value = true; };
template <> struct TIsBitwiseConstructible<uint64_t,  int64_t> { static constexpr bool Value = true; };
template <> struct TIsBitwiseConstructible< int64_t, uint64_t> { static constexpr bool Value = true; };

}
