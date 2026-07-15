// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SequencerAnimTools: ModuleRules
{
	public SequencerAnimTools (ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
                "Slate",
                "SlateCore",
				"ControlRig",
				"ControlRigEditor"
			}
		);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Constraints",
				"Engine",
				"InputCore",
				"UnrealEd",
				"LevelEditor",
				
				"EditorFramework",
                "EditorInteractiveToolsFramework",
                "InteractiveToolsFramework",

                "MovieScene",
                "MovieSceneTracks",
                "MovieSceneTools",
                "Sequencer",
				"LevelSequence",
				"LevelSequenceEditor",
			}
        );
	}
}
