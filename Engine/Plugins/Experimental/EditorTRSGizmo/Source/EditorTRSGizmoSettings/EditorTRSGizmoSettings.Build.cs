// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class EditorTRSGizmoSettings : ModuleRules
{
    public EditorTRSGizmoSettings(ReadOnlyTargetRules Target) : base(Target)
    {
        // Enable truncation warnings in this module
        CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Warning;

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "DeveloperSettings",
                "EditorInteractiveToolsFramework",
                "Engine",
                "InputCore",
                "InteractiveToolsFramework",
                "Json",
                "JsonUtilities",
                "PropertyEditor",
                "Slate",
                "SlateCore",
                "ToolWidgets",
                "UnrealEd",
                "WorkspaceMenuStructure", 
            }
        );
    }
}
