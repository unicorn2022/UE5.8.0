// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class DisplayClusterMonitorEditor : ModuleRules
	{
		public DisplayClusterMonitorEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"DisplayClusterMedia",
					"DisplayClusterMonitor",
					"EditorStyle",
					"EditorWidgets",
					"InputCore",
					"LevelEditor",
					"Media",
					"MediaAssets",
					"MediaPlayerEditor",
					"NDIMedia",
					"OutputLog",
					"Projects",
					"Slate",
					"SlateCore",
					"ToolMenus",
					"ToolWidgets",
					"UnrealEd",
					"WorkspaceMenuStructure",
				});
		}
	}
}
