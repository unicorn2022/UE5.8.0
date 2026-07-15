// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MaterialValidation : ModuleRules
	{
		public MaterialValidation(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"AssetDefinition",
				"Core",
				"CoreUObject",
				"DataValidation",
				"DeveloperSettings",
				"Engine",
				"Projects",
				"PropertyEditor",
				"InputCore",
				"MaterialEditor",
				"RHI",
				"Slate",
				"SlateCore",
				"SourceControl",
				"UnrealEd",
			});
		}
	}
}