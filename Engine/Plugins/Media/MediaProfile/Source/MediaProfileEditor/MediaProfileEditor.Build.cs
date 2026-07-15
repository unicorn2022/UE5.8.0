// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class MediaProfileEditor : ModuleRules
    {
        public MediaProfileEditor(ReadOnlyTargetRules Target) : base(Target)
        {
            PublicDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core",
                }
            );

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "AssetDefinition",
                    "CoreUObject",
                    "Engine",
                    "MediaAssets",
                    "MediaIOCore",
                    "MediaIOEditor",
                    "MediaPlayerEditor",
                    "MediaProfile",
                    "Projects",
                    "PropertyEditor",
                    "Slate",
                    "SlateCore",
                    "UnrealEd"
                }
            );
        }
    }
}