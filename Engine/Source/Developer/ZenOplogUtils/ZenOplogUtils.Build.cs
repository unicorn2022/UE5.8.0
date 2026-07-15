// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class ZenOplogUtils : ModuleRules
{
	public ZenOplogUtils(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "Zen", "Json" });
		PrivateIncludePathModuleNames.AddRange(new string[] { "DesktopPlatform" });
		CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Error;
	}
}
