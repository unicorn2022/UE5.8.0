// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

namespace UE::UAF
{

// Dependency type passed to AddDependency in FSystemReference/FUAFWeakSystemReference etc.
enum class ESystemDependency : uint8
{
	// Dependency runs before the specified event
	Prerequisite,
	// Dependency runs after the specified event
	Subsequent
};

}