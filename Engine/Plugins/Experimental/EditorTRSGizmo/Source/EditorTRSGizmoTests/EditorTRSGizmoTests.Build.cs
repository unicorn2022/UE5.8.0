// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class EditorTRSGizmoTests : ModuleRules
{
    public EditorTRSGizmoTests(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "AutomationDriver",
                "Core",
                "CoreUObject",
                "CQTest",
                "EditorInteractiveToolsFramework",
                "Engine",
                "InputCore",
                "InteractiveToolsFramework",
                "Json",
                "MessageLog",
                "Networking",
                "Slate",
                "SlateCore",
                "TypedElementFramework",
                "TypedElementRuntime",
                "UnrealEd", 
                "WebSockets",
            }
        );
    }
}
