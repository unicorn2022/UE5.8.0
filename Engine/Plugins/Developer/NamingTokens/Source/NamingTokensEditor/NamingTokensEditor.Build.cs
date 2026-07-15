// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NamingTokensEditor : ModuleRules
{
    public NamingTokensEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "NamingTokens",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
	            "AssetDefinition",
	            "BlueprintGraph",
	            "Blutility",
                "CoreUObject",
                "EditorToolEvents",
                "Engine",
                "EngineAssetDefinitions",
                "InputCore",
                "Slate",
                "SlateCore",
                "Projects",
                "UnrealEd",
            }
        );
    }
}