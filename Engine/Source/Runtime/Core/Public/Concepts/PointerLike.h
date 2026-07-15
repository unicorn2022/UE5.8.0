// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Concepts/ConditionallyConvertibleToBool.h"
#include "Concepts/Dereferenceable.h"

namespace UE
{
	/**
	 * Concept which describes a type which acts like a pointer - notably is dereferenceable and is bool-testable for null.
	 */
	template <typename T>
	concept CPointerLike = UE::CConditionallyConvertibleToBool<T> && UE::CDereferenceable<T>;
}
