// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PlayerInputDebugger : ModuleRules
{
	public PlayerInputDebugger(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[]
			{

			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ApplicationCore",
				"CommonInput",
				"CommonUI",
				"Core",
				"CoreUObject",
				"Engine",
				"EnhancedInput",
				"GameplayTags",
				"InputCore",
				"EditorWidgets",
				"PropertyEditor",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UMG",
				"UnrealEd",
				"WorkspaceMenuStructure",
			});
	}
}
