// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>

namespace UE
{
	/**
	 * Concept which describes an enum type.
	 */
	template <typename T>
	concept CEnum = std::is_enum_v<T>;
}
