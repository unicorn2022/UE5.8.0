// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PhysicsProxyHandle.h"

#include "Chaos/ParticleHandle.h"

namespace Chaos
{
	void FPhysicsProxyHandle::Set(FGeometryParticleHandle& ParticleHandle)
	{
		Set(ParticleHandle.PhysicsProxy());
	}
}
