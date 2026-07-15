// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosRigidAssetNodesModule.h"
#include "ShapeElemNodes.h"
#include "PhysicsAssetDataflowNodes.h"
#include "AnimationTerminalNodes.h"
#include "Generators/SimulationAffector.h"

IMPLEMENT_MODULE(FChaosRigidAssetNodesModule, ChaosRigidAssetNodes);

 void FChaosRigidAssetNodesModule::StartupModule()
{
	UE::Dataflow::RegisterSimulationAffectorNodes();
	UE::Dataflow::RegisterPhysicsAssetTerminalNode();
	UE::Dataflow::RegisterPhysicsAssetNodes();
	UE::Dataflow::RegisterShapeNodes();
	UE::Dataflow::RegisterAnimationTerminalNodes();
 }

 void FChaosRigidAssetNodesModule::ShutdownModule()
{

}
