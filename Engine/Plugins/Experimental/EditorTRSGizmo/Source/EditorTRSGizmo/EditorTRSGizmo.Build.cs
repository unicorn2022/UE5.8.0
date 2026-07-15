// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class EditorTRSGizmo : ModuleRules
{
	public EditorTRSGizmo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"DeveloperSettings",
				"EditorInteractiveToolsFramework",
				"GeometryCore"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AnimationCore",
				"CoreUObject",
				"EditorFramework",
				"Engine",
				"InputCore",
				"InteractiveToolsFramework",
				"RenderCore",
				"Slate",
				"SlateCore",
				"TypedElementFramework",
				"TypedElementRuntime",
				"UnrealEd"
			});
	}
}
