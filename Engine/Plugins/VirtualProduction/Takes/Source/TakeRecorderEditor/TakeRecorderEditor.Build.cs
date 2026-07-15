// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TakeRecorderEditor : ModuleRules
{
	public TakeRecorderEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"AppFramework",
				"AssetDefinition",
				"AssetRegistry",
				"TakeTrackRecorders",
				"ClassViewer",
				"ContentBrowser",
				"Core",
				"CoreUObject",
				"Engine",
				"EditorStyle",
				"EditorWidgets",
				"LevelEditor",
				"MovieSceneTools",
				"PropertyEditor",
				"EditorFramework",
				"MovieScene",
				"MovieSceneTracks",
				"LevelSequence",
				"InputCore",
				"MessageLog",
				"UnrealEd",
				"WorkspaceMenuStructure",
				"SequencerCore",
				"Slate",
				"SlateCore",
				"NamingTokens",
				"NamingTokensUI",
				"TakeRecorder",
				"TakesCore",
				"TakeMovieScene",
				"ToolWidgets",
				"ToolMenus",
				"TimeManagement",
				"Projects"
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"EditorSubsystem",
				"Sequencer"
			}
		);
	}
}
