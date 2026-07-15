// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SlateBaseRenderer : ModuleRules
{
	public SlateBaseRenderer(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Slate",
				"SlateCore"
			}
		);

		if (Target.bCompileAgainstEngine)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Engine"
				});
		}
	}
}
