// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>

namespace UE
{
	/**
	 * Concept which describes an unsigned type.
	 */
	template <typename T>
	concept CUnsigned = std::is_unsigned_v<T>;
}
