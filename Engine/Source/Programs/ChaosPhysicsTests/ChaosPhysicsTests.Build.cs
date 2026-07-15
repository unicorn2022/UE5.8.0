// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ChaosPhysicsTests : TestModuleRules
{
	static ChaosPhysicsTests()
	{
		if (InTestMode)
		{
			TestMetadata = new Metadata();
			TestMetadata.TestName = "ChaosPhysics";
			TestMetadata.TestShortName = "ChaosPhysics";
			TestMetadata.ReportType = "testdata";
			TestMetadata.SupportedPlatforms.Add(UnrealTargetPlatform.Mac);
			// TODO: These don't currently work. Need to investigate later, but disabling to get something working for now.
			//TestMetadata.SupportedPlatforms.Add(UnrealTargetPlatform.Linux);
			//TestMetadata.SupportedPlatforms.Add(UnrealTargetPlatform.Android);
			//TestMetadata.SupportedPlatforms.Add(UnrealTargetPlatform.IOS);

			string PlatformCompilationArgs;
			foreach (var Platform in UnrealTargetPlatform.GetValidPlatforms())
			{
				if (Platform == UnrealTargetPlatform.Android)
				{
					PlatformCompilationArgs = "-allmodules -architectures=arm64";
				}
				else
				{
					PlatformCompilationArgs = "-allmodules";
				}
				TestMetadata.PlatformCompilationExtraArgs.Add(Platform, PlatformCompilationArgs);
				// Add our test tag to limit tests
				TestMetadata.PlatformTags.Add(Platform, "[@ChaosTests]");
			}

			// Platform-specific tags
			TestMetadata.PlatformTags[UnrealTargetPlatform.Linux] += "~[.]~[Slow]";
			TestMetadata.PlatformTags[UnrealTargetPlatform.Android] += "~[Perf]~[Slow]~[AndroidSkip]";

			// Allow Android run for this test
			// Will remove Android from PlatformsRunUnsupported as more diverse types of tests can run on this platform
			TestMetadata.PlatformsRunUnsupported.Remove(UnrealTargetPlatform.Android);
		}
	}

	public ChaosPhysicsTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"ChaosTestHarness",
		});

		// Temporarily link to base Chaos modules - remove once everything is moved into the new tree
		SetupModulePhysicsSupport(Target);

		bTreatAsEngineModule = true;
		bWarningsAsErrors = true;
	}
}