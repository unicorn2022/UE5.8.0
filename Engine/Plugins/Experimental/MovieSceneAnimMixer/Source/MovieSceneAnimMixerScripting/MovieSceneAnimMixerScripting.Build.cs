// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MovieSceneAnimMixerScripting : ModuleRules
	{
		public MovieSceneAnimMixerScripting(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"MovieScene",
					"MovieSceneTracks",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CoreUObject",
					"Engine",
					"MovieSceneAnimMixer",
				}
			);
		}
	}
}
