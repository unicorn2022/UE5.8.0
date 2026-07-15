// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DataLinkJsonEditor : ModuleRules
{
    public DataLinkJsonEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
                "Json",
			}
		);

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "AssetTools",
                "BlueprintGraph",
                "ContentBrowser",
                "Core",
                "CoreUObject",
                "DataLinkEditor",
                "Engine",
                "JsonUtilities",
                "Slate",
                "SlateCore",
                "ToolMenus",
                "UnrealEd",
            }
        );
    }
}