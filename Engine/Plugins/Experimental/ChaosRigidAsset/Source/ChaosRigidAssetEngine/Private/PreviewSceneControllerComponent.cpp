// Copyright Epic Games, Inc. All Rights Reserved.

#include "PreviewSceneControllerComponent.h"

#include "Chaos/ChaosScene.h"
#include "ChaosSolverConfiguration.h"
#include "Engine/World.h"
#include "PBDRigidsSolver.h"
#include "Physics/Experimental/PhysScene_Chaos.h"

void UPreviewSceneControllerComponent::ApplySolverConfig(const FChaosSolverConfiguration& Config)
{
	if(UWorld* World = GetWorld())
	{
		if(FPhysScene* PhysScene = World->GetPhysicsScene())
		{
			PhysScene->GetSolver()->ApplyConfig(Config);
		}
	}
}

void UPreviewSceneControllerComponent::EnableAsyncTick(float InAsyncDt)
{
	if(UWorld* World = GetWorld())
	{
		if(FPhysScene* PhysScene = World->GetPhysicsScene())
		{
			PhysScene->GetSolver()->EnableAsyncMode(InAsyncDt);
		}
	}
}

void UPreviewSceneControllerComponent::DisableAsyncTick()
{
	if(UWorld* World = GetWorld())
	{
		if(FPhysScene* PhysScene = World->GetPhysicsScene())
		{
			PhysScene->GetSolver()->DisableAsyncMode();
		}
	}
}
