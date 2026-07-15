// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AudioSynesthesiaEditor : ModuleRules
{
	public AudioSynesthesiaEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"AudioAnalyzer",
				"AudioEditor",
				"AudioSynesthesia",
				"Core",
				"CoreUObject",
				"Engine",
				"EditorFramework",
				"EditorStyle",
				"InputCore",
				"Slate",
				"SlateCore",
				"UnrealEd",
            }
        );

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"AssetDefinition"
			});
	}
}
