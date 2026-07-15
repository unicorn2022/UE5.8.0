// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SourceControlViewport : ModuleRules
{
	public SourceControlViewport(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"CoreUObject",
				"DeveloperSettings",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"Engine",
				"InputCore",
				"LevelEditor",
				"Slate",
				"SlateCore",
				"SourceControl",
				"ToolMenus",
				"UnrealEd"
			}
		);
	}
}
