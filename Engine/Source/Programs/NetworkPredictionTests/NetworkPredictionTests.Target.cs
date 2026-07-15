// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedPlatforms("Win64", "Linux")]
public class NetworkPredictionTestsTarget : TestTargetRules
{
	public NetworkPredictionTestsTarget(TargetInfo Target) : base(Target)
	{
		bTestsRequireEngine = true;
		bTestsRequireApplicationCore = true;
		bUsePlatformFileStub = true;
		bMockEngineDefaults = true;

		bUsesSlate = false;

		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
	}
}
