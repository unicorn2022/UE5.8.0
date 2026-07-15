// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using UnrealBuildTool.Rules;

public class CaptureManagerDeviceBlueprint : ModuleRules
{
	public CaptureManagerDeviceBlueprint(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CaptureManagerCPSClient",
			"CaptureProtocolStack",
			"CaptureUtils",
		});

		ShortName = "CapManDevBp";
	}
}
