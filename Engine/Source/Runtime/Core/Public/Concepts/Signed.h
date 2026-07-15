// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>

namespace UE
{
	/**
	 * Concept which describes a signed type.
	 */
	template <typename T>
	concept CSigned = std::is_signed_v<T>;
}
