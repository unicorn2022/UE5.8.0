// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GDKSaveGameSystem : ModuleRules
{
	public GDKSaveGameSystem( ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[]
			{
				"Core",
				"Engine",
				"ApplicationCore",
				"GRDK",
				"GDKRuntime",
			});
	}
}
