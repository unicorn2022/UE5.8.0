// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VEUVEditor : ModuleRules
{
	public VEUVEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"VEUVCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"UnrealEd",
			"WorkspaceMenuStructure",
			"ToolMenus",
			"InputCore",
			"PropertyEditor",
		});
	}
}
