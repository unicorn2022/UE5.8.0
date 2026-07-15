// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GDKPackageManifest : ModuleRules
{
	public GDKPackageManifest( ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Json",
				"GRDK",
			}
		);
	}
}
