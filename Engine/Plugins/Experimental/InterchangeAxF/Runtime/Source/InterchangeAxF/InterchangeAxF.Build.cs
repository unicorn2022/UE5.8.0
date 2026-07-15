// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class InterchangeAxF : ModuleRules
{
    public InterchangeAxF(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		//OptimizeCode = CodeOptimization.Never;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
			}
            );


        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "UnrealEd",
                "MaterialEditor",
                "InterchangeCore",
                "InterchangeEngine",
                "InterchangeImport",
                "InterchangeNodes",
                "InterchangeFactoryNodes",
                "InterchangePipelines",
                "InterchangeAxFAssets",
                "Json"
            }
            );
    }
}
