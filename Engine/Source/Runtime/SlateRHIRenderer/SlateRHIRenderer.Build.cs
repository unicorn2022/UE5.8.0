// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SlateRHIRenderer : ModuleRules
{
    public SlateRHIRenderer(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"SlateBaseRenderer"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"DeveloperSettings",
				"Slate",
				"SlateCore",
                "Engine",
                "RHI",
                "RenderCore",
				"Renderer",
                "ImageCore",
				"HeadMountedDisplay"
			}
		);
	}
}
