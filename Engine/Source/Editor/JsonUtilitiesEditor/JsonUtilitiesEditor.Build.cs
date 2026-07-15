// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class JsonUtilitiesEditor : ModuleRules
{
    public JsonUtilitiesEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(
	        new string[]
	        {
		        "Core",
		        "CoreUObject",
		        "Engine",
		        "UnrealEd",
		        "Slate",
		        "SlateCore",
		        "Json",
		        "JsonUtilities",
		        "BlueprintGraph",
		        "AssetTools",
	        }
        );
       
        PrivateIncludePaths.Add(
	        System.IO.Path.Combine(GetModuleDirectory("JsonUtilities"), "Private")
	    );
    }
}