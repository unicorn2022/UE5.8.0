// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TmvMediaShaders : ModuleRules
{
	public TmvMediaShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[] {
			});

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"Projects",
				"RenderCore",
				"RHI"
			});
	}
}