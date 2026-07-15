// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RTSPMediaEditor : ModuleRules
{
    public RTSPMediaEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "AssetDefinition",
            "Core",
            "CoreUObject",
            "MediaPlayerEditor",
            "Projects",
            "RTSPMedia",
            "Slate",
            "SlateCore",
            "UnrealEd"
        });
    }
}