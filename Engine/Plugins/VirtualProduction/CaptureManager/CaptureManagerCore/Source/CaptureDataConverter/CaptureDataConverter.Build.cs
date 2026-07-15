// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class CaptureDataConverter : ModuleRules
{
	public CaptureDataConverter(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CaptureUtils",
			"CaptureManagerTakeMetadata",
			"CaptureManagerPipeline",
			"CaptureManagerMediaRW"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"CaptureDataUtils",
			"ImageWrapper",
			"Json",
			"DataIngestCore",
			"Media",
			"NamingTokens"
		});
	}
}
