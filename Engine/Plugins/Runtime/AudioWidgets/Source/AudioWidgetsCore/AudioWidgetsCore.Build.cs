// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AudioWidgetsCore : ModuleRules
{
	public AudioWidgetsCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange
		(
			new string[]
			{
				"Core",
			}
		);

		PrivateDependencyModuleNames.AddRange
		(
			new string[]
			{
				"AppFramework",
				"AudioMixerCore",
				"CoreUObject",
				"InputCore",
				"Slate",
				"SlateCore",
			}
		);

		bDisableAutoRTFMInstrumentation = true;
	}
}
