// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedPlatforms(UnrealPlatformClass.All)]
public class UAFChooserTestsTarget : TestTargetRules
{
	public UAFChooserTestsTarget(TargetInfo Target) : base(Target)
	{
		bUsePlatformFileStub = true;
		bMockEngineDefaults = true;

		bCompileAgainstEngine = true;
		bCompileAgainstApplicationCore = true;
		
		bTestsRequireEngine = true;
		
		bWarningsAsErrors = true;

	    // Setup like any other target: set compilation flags, global definitions etc.
        GlobalDefinitions.Add("CATCH_CONFIG_ENABLE_BENCHMARKING=1");
	}
}
