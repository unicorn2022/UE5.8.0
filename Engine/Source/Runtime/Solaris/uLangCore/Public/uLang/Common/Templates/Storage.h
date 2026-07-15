// Copyright Epic Games, Inc. All Rights Reserved.
// Templates for memory, storage, containers and alignment

#pragma once

#include "uLang/Common/Common.h"
#include "uLang/Common/Containers/RangeView.h"
#include "uLang/Common/Templates/Invoke.h"
#include "uLang/Common/Templates/TypeTraits.h"
#include "uLang/Common/Memory/MemoryOps.h"

#include <initializer_list>
#include <type_traits>
#include <new>

#include <string.h>

namespace uLang
{

//------------------------------------------------------------------
// From UnrealTemplate.h

/**
 * Generically gets the data pointer of a contiguous container
 * Named differently from GetData() in UnrealTemplate.h to avoid ambiguous overload resolution.
 */
template<typename T>
requires (TIsContiguousContainer<T>::Value)
auto ULangGetData(T&& Container) -> decltype(Container.GetData())
{
    return Container.GetData();
}

template <typename T, size_t N>
constexpr T* ULangGetData(T (&Container)[N])
{
    return Container;
}

template <typename T>
constexpr T* ULangGetData(std::initializer_list<T> List)
{
    return List.begin();
}

template <typename FirstIterator, typename LastIterator>
constexpr FirstIterator ULangGetData(const TRangeView<FirstIterator, LastIterator>& View)
{
    return View.begin();
}

/**
* Generically gets the number of items in a contiguous container
 * Named differently from GetNum() in UnrealTemplate.h to avoid ambiguous overload resolution.
*/
template<typename T>
requires (TIsContiguousContainer<T>::Value)
size_t ULangGetNum(T&& Container)
{
    return (size_t)Container.Num();
}

template <typename T, size_t N>
constexpr size_t ULangGetNum(T (&Container)[N])
{
    return N;
}

template <typename T>
constexpr size_t ULangGetNum(std::initializer_list<T> List)
{
    return List.size();
}

template <typename FirstIterator, typename LastIterator>
constexpr int32_t ULangGetNum(const TRangeView<FirstIterator, LastIterator>& View)
{
    return View.Num();
}

//------------------------------------------------------------------
// From AlignmentTemplates.h

/**
 * Aligns a value to the nearest higher multiple of 'Alignment', which must be a power of two.
 *
 * @param  Val        The value to align.
 * @param  Alignment  The alignment value, must be a power of two.
 *
 * @return The value aligned up to the specified alignment.
 */
template <typename T>
constexpr T AlignUp(T Val, uint64_t Alignment)
{
    static_assert(std::is_integral_v<T> || std::is_pointer_v<T>, "Align expects an integer or pointer type");

    return (T)(((uint64_t)Val + Alignment - 1) & ~(Alignment - 1));
}

//------------------------------------------------------------------
// From TypeCompatibleBytes.h

/** An untyped array of data with compile-time alignment and size derived from another type. */
template<typename ElementType>
struct TTypeCompatibleBytes
{
    ElementType& Get()
    {
        return *std::launder(reinterpret_cast<ElementType*>(_Bytes));
    }
    const ElementType& Get() const
    {
        return *std::launder(reinterpret_cast<const ElementType*>(_Bytes));
    }

private:
    alignas(ElementType) uint8_t _Bytes[sizeof(ElementType)];
};

//------------------------------------------------------------------
// From UnrealTemplate.h

/**
 * A traits class which specifies whether a Swap of a given type should swap the bits or use a traditional value-based swap.
 */
template <typename T>
struct TUseBitwiseSwap
{
    // We don't use bitwise swapping for 'register' types because this will force them into memory and be slower.
    static constexpr bool Value = !(std::is_enum_v<T> || std::is_pointer_v<T> || std::is_arithmetic_v<T>);
};


/**
 * Swap two values.  Assumes the types are trivially relocatable.
 */
template <typename T>
requires (TUseBitwiseSwap<T>::Value)
inline void Swap(T& A, T& B)
{
    if (ULANG_LIKELY(&A != &B))
    {
        TTypeCompatibleBytes<T> Temp;
        ULANG_IGNORE_CLASS_MEMACCESS_WARNING_START
        memcpy((void*)&Temp, &A, sizeof(T));
        memcpy((void*)&A, &B, sizeof(T));
        memcpy((void*)&B, &Temp, sizeof(T));
        ULANG_IGNORE_CLASS_MEMACCESS_WARNING_END
    }
}

template <typename T>
requires (!TUseBitwiseSwap<T>::Value)
inline void Swap(T& A, T& B)
{
    T Temp = uLang::Move(A);
    A = uLang::Move(B);
    B = uLang::Move(Temp);
}

/**
 * utility template for a class that should not be copyable.
 * Derive from this class to make your class non-copyable
 */
class CNoncopyable
{
protected:
	// ensure the class cannot be constructed directly
	CNoncopyable() = default;
	// the class should not be used polymorphically
	~CNoncopyable() = default;

private:
	CNoncopyable(const CNoncopyable&) = delete;
    CNoncopyable(CNoncopyable&&) = delete;
	CNoncopyable& operator=(const CNoncopyable&) = delete;
    CNoncopyable& operator=(CNoncopyable&&) = delete;
};

/** 
 * exception-safe guard around saving/restoring a value.
 * Commonly used to make sure a value is restored 
 * even if the code early outs in the future.
 * Usage:
 *  	TGuardValue<bool> GuardSomeBool(bSomeBool, false); // Sets bSomeBool to false, and restores it in dtor.
 */
template <typename RefType, typename AssignedType = RefType>
struct TGuardValue : private CNoncopyable
{
	TGuardValue(RefType& ReferenceValue, const AssignedType& NewValue)
	    : RefValue(ReferenceValue), OldValue(Move(ReferenceValue))
	{
		RefValue = NewValue;
	}

    explicit TGuardValue(RefType& ReferenceValue)
        : RefValue(ReferenceValue), OldValue(ReferenceValue)
    {
    }

	~TGuardValue()
	{
		RefValue = Move(OldValue);
	}

	/**
	 * Overloaded dereference operator.
	 * Provides read-only access to the original value of the data being tracked by this struct
	 *
	 * @return	a const reference to the original data value
	 */
	const AssignedType& operator*() const
	{
		return OldValue;
	}

private:
	RefType& RefValue;
	AssignedType OldValue;
};

template <typename Function>
struct TGuard : private CNoncopyable
{
    template <typename... TArgs>
    requires (std::is_constructible_v<Function, TArgs&&...>)
    explicit TGuard(TArgs&&... Args)
        : _Function(uLang::ForwardArg<TArgs>(Args)...)
    {
    }

    ~TGuard()
    {
        uLang::Invoke(_Function);
    }

private:
    Function _Function;
};

template <typename Function>
TGuard(Function&&) -> TGuard<std::decay_t<Function>>;
}
