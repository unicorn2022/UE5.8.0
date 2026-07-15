// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CaptureManagerCPSClient : ModuleRules
{
	public CaptureManagerCPSClient(ReadOnlyTargetRules Target) : base(Target)
	{
		ShortName = "CMCPSClient";

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CaptureUtils",
			"CaptureProtocolStack",
		});
	}
}
