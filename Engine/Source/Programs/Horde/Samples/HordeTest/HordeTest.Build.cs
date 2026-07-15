// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules;

[SupportedPlatforms(UnrealPlatformClass.Desktop)]
public class HordeTest : ModuleRules
{
	public HordeTest(ReadOnlyTargetRules target) : base(target)
	{
		PrivateDependencyModuleNames.AddRange([
			"Core",
			"Horde",
			"DesktopPlatform",
			"HTTP"
		]);
	}
}