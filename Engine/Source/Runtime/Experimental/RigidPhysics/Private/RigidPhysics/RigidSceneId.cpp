// Copyright Epic Games, Inc. All Rights Reserved.
#include "RigidPhysics/RigidSceneId.h"

#if UE_RIGIDPHYSICS_API_ENABLED

#include "RigidPhysics/RigidScene.h"
#include "RigidPhysics/RigidSceneRegistry.h"

namespace UE::Physics
{
	IRigidScene* FRigidSceneId::Get() const
	{
		FRigidSceneRegistry& Registry = FRigidSceneRegistry::GetInstance();
		return Registry.Get(*this);
	}

	void FRigidSceneId::Reset()
	{
		Id = INDEX_NONE;
	}

	uint32 GetTypeHash(const FRigidSceneId& InId)
	{
		return ::GetTypeHash(InId.Id);
	}

} // namespace UE::Physics

#endif // UE_RIGIDPHYSICS_API_ENABLED
