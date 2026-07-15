// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanBodyTrackerInterface : ModuleRules
{
	public MetaHumanBodyTrackerInterface(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"CaptureDataCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"MetaHumanCoreTech",
			"MetaHumanPipelineCore",
		});
	}
}
