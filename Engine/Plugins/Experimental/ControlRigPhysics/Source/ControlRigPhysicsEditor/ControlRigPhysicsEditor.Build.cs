// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ControlRigPhysicsEditor : ModuleRules
{
	public ControlRigPhysicsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ControlRig",
				"ControlRigPhysics",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"ToolMenus",
				"WorkspaceMenuStructure",
			}
		);
	}
}
