// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>

#include "Concepts/Array.h"
#include "Concepts/BoundedArray.h"

namespace UE
{
	/**
	 * Concept which describes an unbounded array type.
	 */
	template <typename T>
	concept CUnboundedArray = UE::CArray<T> && !UE::Private::BoundedArray::CBoundedArrayImpl<const volatile T>;
}
