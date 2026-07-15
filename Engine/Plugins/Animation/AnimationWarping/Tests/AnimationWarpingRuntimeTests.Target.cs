// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedPlatforms(UnrealPlatformClass.All)]
public class AnimationWarpingRuntimeTestsTarget : TestTargetRules
{
	public AnimationWarpingRuntimeTestsTarget(TargetInfo Target) : base(Target)
	{
		bUsePlatformFileStub = true;
		bMockEngineDefaults = true;

		bCompileAgainstEngine = true;
		bCompileAgainstApplicationCore = true;

		bTestsRequireEngine = true;
	}
}
