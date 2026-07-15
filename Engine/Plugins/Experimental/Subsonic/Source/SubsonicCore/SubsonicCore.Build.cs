// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SubsonicCore : ModuleRules
{
	public SubsonicCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"AudioMixerCore",
				"Core",
				"GameplayTags",
				"PropertyBindingUtils"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
			}
		);

		bAllowUETypesInNamespaces = true;
	}
}
