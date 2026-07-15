// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AutomatedPerfTestLaunchExtension : ModuleRules
{
	public AutomatedPerfTestLaunchExtension(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
		[
			"Core",
			"CoreUObject",
			"Json",
			"Slate",
			"SlateCore",
			"Sockets",
			"TraceLog",
			"ProjectLauncher",
			"DeveloperSettings",
			"ToolWidgets",
		]);


		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
			[
				"UnrealEd",
				"Engine"
			]);
		}
	}
}
