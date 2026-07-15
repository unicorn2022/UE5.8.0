// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GDKPackageChunkInstall : ModuleRules
{
    public GDKPackageChunkInstall(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(
            new string[] 
            {
                "Core",
				"GRDK",
                "GDKPackageManifest",
				"GDKRuntime",
            }
        );
    }
}