// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class InputBindingSettingsViewer : ModuleRules
{
    public InputBindingSettingsViewer(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "EditorFramework",
                "SettingsEditor",
                "UnrealEd",
				"Slate",
				"SlateCore",
            }
        );
    }
}