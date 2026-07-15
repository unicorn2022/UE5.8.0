// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MovieSceneMixedControlRig : ModuleRules
	{
		public MovieSceneMixedControlRig(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"DeveloperSettings",
					"MovieScene",
					"MovieSceneTracks",
					"AnimGraphRuntime",
					"MovieSceneAnimMixer",
					"ControlRig",
					"UAFControlRig",
					"RigVM",
					"AnimationCore",
					"UAF",
					"UAFAnimGraph"
				}
			);
		}
	}
}