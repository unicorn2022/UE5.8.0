// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class FastBuildController : ModuleRules
{
	public FastBuildController(ReadOnlyTargetRules TargetRules)
		: base(TargetRules)
	{
		bRequiresPlatformSDK = true;
		
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"RenderCore"
		});

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"DistributedBuildInterface",
				"TargetPlatform"
			}
		);
	}
}
