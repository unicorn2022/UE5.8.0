// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AnimationWarpingRuntimeTests : TestModuleRules
{
	static AnimationWarpingRuntimeTests()
	{
		if (InTestMode)
		{
			TestMetadata = new Metadata();
			TestMetadata.TestName = "AnimationWarpingRuntime";
			TestMetadata.TestShortName = "Animation Warping Runtime";
		}
	}

	public AnimationWarpingRuntimeTests(ReadOnlyTargetRules Target) : base(Target, true)
	{
		bAllowUETypesInNamespaces = true;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AnimationCore",
			"AnimGraphRuntime",
			"AnimationWarpingRuntime",
		});
	}
}
