// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>

namespace UE
{
	/**
	 * Concept which describes a const type.
	 */
	template <typename T>
	concept CConst = std::is_const_v<T>;
}
