// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Concepts/Array.h"
#include "Templates/IsArray.h"

namespace UE::Private::BoundedArray
{
	// This helper concept is used by both UE::CBoundedArray and UE::CUnboundedArray to ensure that the compiler
	// knows that bounded array and unbounded array types are mutually exclusive.
	template <typename T>
	concept CBoundedArrayImpl = TIsBoundedArray<T>::Value;
}

namespace UE
{
	/**
	 * Concept which describes a bounded array type.
	 */
	template <typename T>
	concept CBoundedArray = UE::CArray<T> && UE::Private::BoundedArray::CBoundedArrayImpl<const volatile T>;
}
