// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedTargetTypes(TargetType.Editor, TargetType.Program)]
public class ActorPickerMode : ModuleRules
{
    public ActorPickerMode(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
                "InputCore",
                "InteractiveToolsFramework",
                "Slate",
                "SlateCore",
				"EditorFramework",
				"UnrealEd",
				"EditorInteractiveToolsFramework",
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				"LevelEditor",
			}
		);
	}
}
