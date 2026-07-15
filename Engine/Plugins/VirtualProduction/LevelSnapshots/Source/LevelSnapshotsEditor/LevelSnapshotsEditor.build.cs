// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class LevelSnapshotsEditor : ModuleRules
{
	public LevelSnapshotsEditor(ReadOnlyTargetRules Target) : base(Target)
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
				"ApplicationCore",
				"AssetDefinition",
				"AssetRegistry",
				"AssetTools",
				"BlueprintGraph",
				"CoreUObject",
				"ContentBrowser",
				"Engine",
				"EngineAssetDefinitions",
				"EditorFramework",
				"EditorStyle",
				"EditorWidgets",
				"GameProjectGeneration",
				"InputCore",
				"Kismet",
				"LevelSnapshots",
				"LevelSnapshotFilters",
				"Projects",
				"PropertyEditor",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"ToolWidgets",
				"UnrealEd",
				"WorkspaceMenuStructure"
			}
			);
	}
}
