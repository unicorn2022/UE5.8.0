// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using EpicGames.Core;

public class MSGamingRuntime : ModuleRules
{
	public MSGamingRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddAll( 
			"GRDK",
			"GDKRuntime",
			"GDKSaveGameSystem",            // requires  [PlatformFeatures] SaveGameSystemModule=GDKSaveGameSystem   in your project's WindowsEngine.ini
			"Core"
		);

		if (Target.bCompileAgainstEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
