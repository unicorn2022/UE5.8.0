// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ChaosRigidAssetEngine : ModuleRules
{
	public ChaosRigidAssetEngine(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Engine",
				"DataflowCore",
				"DataflowEngine",
				"DataflowEnginePlugin"
			}
		);
		
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
			}
		);

		bAllowUETypesInNamespaces = true;

		SetupModulePhysicsSupport(Target);
	}
}
