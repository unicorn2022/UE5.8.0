// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedPlatformGroups("Windows")]
public class MSGameStorePlatformFeatures : ModuleRules
{
	public MSGameStorePlatformFeatures( ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[]
			{
				"Core",
				"Engine",
				"GRDK",
			});

		PublicDependencyModuleNames.AddRange(new string[]
			{
				"WindowsPlatformFeatures",
			});
	}
}
