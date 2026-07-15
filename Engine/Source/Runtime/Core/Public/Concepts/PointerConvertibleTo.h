// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ConvertibleTo.h"

namespace UE
{

/**
 * Concept which describes convertibility of pointers from one subject type to another (i.e. safe upcast).
 */
template <typename From, typename To>
concept CPointerConvertibleTo = UE::CConvertibleTo<From*, To*>;

} // UE
