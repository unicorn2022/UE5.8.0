// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/**
 * Type trait which yields the type of the class given a pointer to a member of that class, e.g.:
 *
 * TMemberPtrOuter_T<decltype(&FVector::X)>   == FVector
 * TMemberPtrOuter_T<decltype(&FVector::Dot)> == FVector
 */
template <typename T>
struct TMemberPtrOuter
{
};

template <typename OuterType, typename MemberType>
struct TMemberPtrOuter<MemberType OuterType::*>
{
	using Type = OuterType;
};

template <typename T>
using TMemberPtrOuter_T = typename TMemberPtrOuter<T>::Type;
