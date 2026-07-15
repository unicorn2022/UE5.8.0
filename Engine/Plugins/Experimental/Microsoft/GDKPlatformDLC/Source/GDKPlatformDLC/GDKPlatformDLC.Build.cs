// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using EpicGames.Core;

public class GDKPlatformDLC : ModuleRules
{
	public GDKPlatformDLC(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddAll( 
			"GRDK",
			"GDKRuntime",
            "GDKPackageManifest",	
			"PlatformDLC",
			"Core",
			"CoreUObject",
			"Engine",
			"Projects",
			"AssetRegistry"
		);

		PublicIncludePathModuleNames.Add("GDKRuntime");
	}
}
