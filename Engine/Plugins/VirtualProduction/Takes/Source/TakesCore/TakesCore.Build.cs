// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TakesCore : ModuleRules
{
	public TakesCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"NamingTokens", 
				"TakeMovieScene",
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"AssetRegistry",
				"LevelSequence",
				"SlateCore",
				"MovieScene",
				"MovieSceneTracks",
				"Engine",
				"SerializedRecorderInterface"
			}
		);
		
		PublicIncludePathModuleNames.AddRange(
			new string[]
			{
				"TakeRecorderNamingTokens",
				"TakeTrackRecorders"
			}
		);

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"EditorFramework",
					"UnrealEd",
				}
			);

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"MovieSceneTools",
					"LevelSequenceEditor",
				}
			);
		}
	}
}
