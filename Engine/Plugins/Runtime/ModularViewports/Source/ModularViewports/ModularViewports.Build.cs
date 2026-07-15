// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ModularViewports : ModuleRules
	{
		public ModularViewports(ReadOnlyTargetRules Target) : base(Target)
		{
			bAllowUETypesInNamespaces = true;

			PublicDependencyModuleNames.AddRange([
				"Core",
				"CoreUObject",
				"DeveloperSettings",
				"Engine",
				"UMG",
			]);

			PrivateDependencyModuleNames.AddRange([
				"ApplicationCore",
				"EngineSettings",
				"InputCore",
				"RenderCore",
				"Renderer",
				"RHI",
				"Slate",
				"SlateCore",
			]);

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.Add("UnrealEd");
			}
		}
	}
}
