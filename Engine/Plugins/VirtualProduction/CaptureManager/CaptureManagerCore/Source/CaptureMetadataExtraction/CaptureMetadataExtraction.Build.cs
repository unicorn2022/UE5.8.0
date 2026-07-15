// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using UnrealBuildTool.Rules;

public class CaptureMetadataExtraction : ModuleRules
{
	public CaptureMetadataExtraction(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"CaptureManagerTakeMetadata"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CaptureUtils",
			"Media",
			"CaptureDataCore",
			"CaptureManagerMediaRW",
			"DataIngestCore",
			"ElectraPlayerPlugin",
			"CaptureDataUtils",
			"Json",
			"MediaAssets"
		});

		ShortName = "CapMetExt";
	}
}
