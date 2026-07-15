// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Concepts/DerivedFrom.h"

class UObject;

namespace UE
{
	/**
	 * Concept which describes a UObject, that is a type derived from UObject.
	 * Pointers are not UObject types - use the following to match pointers to UObjects:
	 *
	 * UE::CPointer<Type> && UE::CSigned<std::remove_pointer_t<Type>>
	 */
	template <typename T>
	concept CUObject = UE::CDerivedFrom<T, UObject>;
}
