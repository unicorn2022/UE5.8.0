// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>

namespace UE
{
	/**
	 * Concept which describes an rvalue reference type.
	 */
	template <typename T>
	concept CRValueReference = std::is_rvalue_reference_v<T>;
}
