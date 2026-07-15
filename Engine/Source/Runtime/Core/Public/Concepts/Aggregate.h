// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>

namespace UE
{
	/**
	 * Concept which describes an aggregate type.
	 */
	template <typename T>
	concept CAggregate = std::is_aggregate_v<T>;
}
