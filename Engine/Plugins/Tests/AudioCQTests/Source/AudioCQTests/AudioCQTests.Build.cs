// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using UnrealBuildTool.Rules;

public class AudioCQTests : ModuleRules
{
	public AudioCQTests(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
					"AudioExtensions",
					"AudioLinkEngine",
					"AudioMixer",
					"Core",
					"CoreUObject",
					"CQTest",
					"Engine",
					"SignalProcessing",
					"SoundFieldRendering",
				 }
			);
	}
}
