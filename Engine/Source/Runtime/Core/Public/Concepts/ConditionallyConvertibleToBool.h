// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Concepts/CompleteType.h"
#include <type_traits>

namespace UE
{
	/**
	 * Concept which describes a type which is conditionally convertible to bool.
	 */
	template <typename T>
	concept CConditionallyConvertibleToBool =
		UE::CCompleteType<std::remove_reference_t<T>> &&
		requires (const T& Val)
		{
			Val ? 1 : 0;
		};
}
