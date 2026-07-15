// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class HarmonixDspTests : ModuleRules
{
	public HarmonixDspTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.Add(System.IO.Path.Combine(GetModuleDirectory("HarmonixDspEditor"), "Private"));

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"Engine",
				"UnrealEd",
				"HarmonixDsp",
				"HarmonixDspEditor",
				"HarmonixMidi",
				"Projects",
				"SignalProcessing",
				"Json"
			}
		);
	}
}
