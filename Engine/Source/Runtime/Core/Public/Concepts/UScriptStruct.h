// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Concepts/CompleteType.h"
#include "Concepts/SameAs.h"

#include <type_traits>

class UScriptStruct;

template <typename T>
struct TBaseStructure;

namespace UE
{
	/**
	 * Concept which describes a UScriptStruct.
	 */
	template <typename T>
	concept CUScriptStruct = UE::CCompleteType<T> &&
		requires
		{
			{ TBaseStructure<std::remove_cv_t<T>>::Get() } -> UE::CSameAs<UScriptStruct*>;
		};
}
