// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ChaosRigidAssetNodes : ModuleRules
{
	public ChaosRigidAssetNodes(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Engine",
				"DataflowCore",
				"DataflowEngine",
				"DataflowEnginePlugin",
				"GeometryCore",
				"DynamicMesh",
				"ChaosRigidAssetEngine"
			}
		);
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
			}
		);

		SetupModulePhysicsSupport(Target);

		bAllowUETypesInNamespaces = true;
	}
}
