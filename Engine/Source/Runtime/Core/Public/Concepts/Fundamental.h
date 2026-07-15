// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>

namespace UE
{
	/**
	 * Concept which describes a fundamental type.
	 */
	template <typename T>
	concept CFundamental = std::is_fundamental_v<T>;
}
