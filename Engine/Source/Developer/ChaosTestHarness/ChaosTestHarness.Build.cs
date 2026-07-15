// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ChaosTestHarness : TestModuleRules
{
	public ChaosTestHarness(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[] {
			"Catch2"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"LowLevelTestsRunner",
		});

		// Temporarily link to base Chaos modules - remove once everything is moved into the new tree
		SetupModulePhysicsSupport(Target);

		bWarningsAsErrors = true;
	}
}