// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System.IO;

public class PCGTests : TestModuleRules
{
	static PCGTests()
	{
		if (InTestMode)
		{
			TestMetadata = new Metadata();
			TestMetadata.TestName = "PCG";
			TestMetadata.TestShortName = "PCG";
		}
	}
	public PCGTests(ReadOnlyTargetRules Target) : base(Target, true)
	{
		bAllowUETypesInNamespaces = true;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"PCG"
			});

		// Allow tests to include PCG private headers (e.g. internal element settings classes
		// that are not part of PCG's public API).
		PrivateIncludePaths.Add(Path.Combine(GetModuleDirectory("PCG"), "Private"));
	}
}