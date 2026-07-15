// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>

namespace UE::Private::Array
{
	template <typename T>
	concept CArrayImpl = std::is_array_v<T>;
}

namespace UE
{
	/**
	 * Concept which describes an array type.
	 */
	template <typename T>
	concept CArray = UE::Private::Array::CArrayImpl<const volatile T>;
}
