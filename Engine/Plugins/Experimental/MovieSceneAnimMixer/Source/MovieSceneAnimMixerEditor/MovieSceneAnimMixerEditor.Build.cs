// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MovieSceneAnimMixerEditor : ModuleRules
	{
		public MovieSceneAnimMixerEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"AnimationCore",
					"AnimGraph",
					"AnimGraphRuntime", 
					"Core",
					"CoreUObject",
					"SequencerCore",
					"Sequencer",
					"MovieSceneAnimMixer",
				}
			);
			
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AssetRegistry",
					"BlueprintGraph",
					"EditorFramework",
					"Engine",
					"InputCore",
					"LevelEditor",
					"LevelSequence",
					"Persona",
					"MovieScene",
					"MovieSceneAnimMixer",
					"MovieSceneTracks",
					"MovieSceneTools",
					"Sequencer",
					"SequencerAnimTools",
					"SequencerCore",
					"SequencerWidgets",
					"Slate",
					"SlateCore",
					"ToolMenus",
					"UAF",
					"UAFEditor",
					"UAFUncookedOnly",
					"UnrealEd",
					"PropertyEditor",
					"EditorInteractiveToolsFramework"
				}
			);
		}
	}
}