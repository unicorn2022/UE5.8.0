// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TimedDataMonitorEditor : ModuleRules
{
	public TimedDataMonitorEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"AssetRegistry",
				"AssetTools",
				"ContentBrowser",
				"Core",
				"CoreUObject",
				"EditorFramework",
				"EditorWidgets",
				"Engine",
				"EditorStyle",
				"InputCore",
				"LiveLink",
				"LiveLinkInterface",
				"MessageLog",
				"Projects",
				"Settings",
				"Slate",
				"SlateCore",
				"TimedDataMonitor",
				"TimeManagement",
				"UnrealEd",
				"WorkspaceMenuStructure",
			});
	}
}
