// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedTargetTypes(TargetType.Editor, TargetType.Program)]
public class InputBindingEditor : ModuleRules
{
	public InputBindingEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"DeveloperSettings",
				"Slate",
				"SlateCore",
				"InputCore",
				"Engine",
				"EditorFramework",
				"UnrealEd",
				"PropertyEditor",
				"Settings",
				"SettingsEditor",
			}
		);
	}
}
