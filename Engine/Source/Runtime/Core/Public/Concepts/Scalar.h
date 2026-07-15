// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>

namespace UE
{
	/**
	 * Concept which describes a scalar type.
	 */
	template <typename T>
	concept CScalar = std::is_scalar_v<T>;
}
