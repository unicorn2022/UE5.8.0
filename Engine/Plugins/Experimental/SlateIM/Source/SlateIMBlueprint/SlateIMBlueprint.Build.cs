// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SlateIMBlueprint : ModuleRules
{
	public SlateIMBlueprint(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"SlateCore",
				"SlateIM",
				"SlateIMEngine",
			}
		);
	}
}
