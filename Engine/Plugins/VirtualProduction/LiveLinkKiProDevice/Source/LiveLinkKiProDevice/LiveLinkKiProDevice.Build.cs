// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LiveLinkKiProDevice : ModuleRules
{
	public LiveLinkKiProDevice(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"LiveLinkDevice",
				"LiveLinkInterface",
				"Projects",
				"HTTP",
				"Json",
				"JsonUtilities",
				"Networking"
			}
		);
	}
}
