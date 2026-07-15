// Copyright Epic Games, Inc. All Rights Reserved.
#include <catch2/catch_test_macros.hpp>

#if UE_RIGIDPHYSICS_API_ENABLED

#include "RigidPhysics/RigidSceneRegistry.h"

GROUP_BEFORE_EACH(Catch::DefaultGroup)
{
	UE::Physics::FRigidSceneRegistry::GetInstance().Reset();
}

#endif // UE_RIGIDPHYSICS_API_ENABLED
