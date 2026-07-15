// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedTargetTypes(TargetType.Editor, TargetType.Program)]
public class StringTableEditor : ModuleRules
{
	public StringTableEditor(ReadOnlyTargetRules Target)
		 : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"AssetDefinition",
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Slate",
				"SlateCore",
				"DesktopPlatform",
				"EditorFramework",
				"UnrealEd",
				"AssetTools",
			});

		DynamicallyLoadedModuleNames.Add("WorkspaceMenuStructure");
	}
}
