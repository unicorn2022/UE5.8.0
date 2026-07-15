// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using UnrealBuildBase;
using UnrealBuildTool;

public class MSGamingSupportEditor : ModuleRules
{
	public MSGamingSupportEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange( 
			new string[] { 
				"Core",
				"CoreUObject",
				"Engine",
				"GDKPlatformEditor",
				"Settings",
				"Slate",
				"SlateCore",
				"Projects",
			}
		);
	}
}
