// Copyright Epic Games, Inc. All Rights Reserved.
// Helper templates for sorting

#pragma once

#include "uLang/Common/Templates/References.h"

#include <type_traits>

namespace uLang
{

//------------------------------------------------------------------
// From IdentityFunctor.h

/**
 * A functor which returns whatever is passed to it.  Mainly used for generic composition.
 */
struct SIdentityFunctor
{
    template <typename T>
    T&& operator()(T&& Val) const
    {
        return (T&&)Val;
    }
};

//------------------------------------------------------------------
// From Less.h

/**
 * Binary predicate class for sorting elements in order.  Assumes < operator is defined for the template type.
 *
 * See: http://en.cppreference.com/w/cpp/utility/functional/less
 */
template <typename T = void>
struct TLess
{
    bool operator()(const T& A, const T& B) const
    {
        return A < B;
    }
};

template <>
struct TLess<void>
{
    template <typename T>
    bool operator()(const T& A, const T& B) const
    {
        return A < B;
    }
};

//------------------------------------------------------------------
// From ReversePredicate.h

/**
 * Helper class to reverse a predicate.
 * Performs Predicate(B, A)
 */
template <typename PredicateType>
class TReversePredicate
{
    const PredicateType& _Predicate;

public:
    TReversePredicate( const PredicateType& Predicate )
        : _Predicate( Predicate )
    {}

    template <typename T>
    bool operator()( T&& A, T&& B ) const { return _Predicate( uLang::ForwardArg<T>(B),  uLang::ForwardArg<T>(A) ); }
};

//------------------------------------------------------------------
// From Sorting.h

/**
 * Helper class for dereferencing pointer types in Sort function
 */
template<class PredicateType>
struct TDereferenceWrapper
{
    const PredicateType& _Predicate;

    TDereferenceWrapper( const PredicateType& Predicate )
        : _Predicate( Predicate ) {}

    /** Pass through for non-pointer types */
    template<class T>
    requires (std::is_pointer_v<T>)
    bool operator()(const T A, const T B) const { return _Predicate(*A, *B); }
    template<class T>
    requires (!std::is_pointer_v<T>)
    bool operator()(const T& A, const T& B) const { return _Predicate(A, B); }
};

}