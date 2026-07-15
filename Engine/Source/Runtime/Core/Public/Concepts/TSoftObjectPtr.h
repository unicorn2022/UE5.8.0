// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Concepts/SameAs.h"

template <typename T>
struct TSoftObjectPtr;

namespace UE::Private::SoftObjectPtr
{
	template <typename T>
	inline constexpr bool TIsTSoftObjectPtr_V = false;

	template <typename T>
	inline constexpr bool TIsTSoftObjectPtr_V<const volatile TSoftObjectPtr<T>> = true;

	template <typename T>
	concept CTSoftObjectPtrImpl = TIsTSoftObjectPtr_V<T>;
}

namespace UE
{
	/**
	 * Concept which describes a TSoftObjectPtr.
	 */
	template <typename T>
	concept CTSoftObjectPtr = UE::Private::SoftObjectPtr::CTSoftObjectPtrImpl<const volatile T>;
}
