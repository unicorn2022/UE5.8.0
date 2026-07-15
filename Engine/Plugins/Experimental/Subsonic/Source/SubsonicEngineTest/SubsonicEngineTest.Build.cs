// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SubsonicEngineTest : ModuleRules
{
	public SubsonicEngineTest(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"CQTest",
				"Engine",
				"GameplayTags",
				"SubsonicCore",
				"SubsonicEngine"
			}
		);

		bAllowUETypesInNamespaces = true;
	}
}
