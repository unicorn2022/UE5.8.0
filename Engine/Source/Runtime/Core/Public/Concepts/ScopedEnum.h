// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Concepts/ConvertibleTo.h"
#include "Concepts/Enum.h"

namespace UE
{
	/**
	 * Concept which describes a scoped enum type (aka. enum class).
	 */
	template <typename T>
	concept CScopedEnum = UE::CEnum<T> && !CConvertibleTo<T, int>;
}
