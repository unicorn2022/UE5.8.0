// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ShaderPlatformConfigEditor : ModuleRules
{
	public ShaderPlatformConfigEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"InputCore",
				"Engine",
				"Slate",
				"SlateCore",
				"RHI",
				"RenderCore",

				"PropertyEditor",
				"UnrealEd",
				"CoreUObject",
				"TargetPlatform",
			}
		);

		PublicIncludePathModuleNames.AddRange(
			new string[] {
				"Engine",
				"RHI",
			}
		);
	}
}
