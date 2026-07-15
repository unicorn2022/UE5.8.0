// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MovieSceneMixedControlRigEditor : ModuleRules
	{
		public MovieSceneMixedControlRigEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"Slate",
					"SlateCore",
					"UnrealEd",
					"EditorFramework",
					"LevelEditor",
					"MovieScene",
					"MovieSceneAnimMixer",
					"MovieSceneTools",
					"Sequencer",
					"ControlRig",
					"ControlRigEditor",
				}
			);
		}
	}
}
