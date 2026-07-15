// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>

namespace UE
{
	/**
	 * Concept which describes an lvalue reference type.
	 */
	template <typename T>
	concept CLValueReference = std::is_lvalue_reference_v<T>;
}
