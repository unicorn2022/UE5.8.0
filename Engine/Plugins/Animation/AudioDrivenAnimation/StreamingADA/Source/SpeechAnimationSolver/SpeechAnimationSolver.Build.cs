// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SpeechAnimationSolver : ModuleRules
{
	public SpeechAnimationSolver(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"NNE",
				"AudioPlatformConfiguration",
			});
	}
}
