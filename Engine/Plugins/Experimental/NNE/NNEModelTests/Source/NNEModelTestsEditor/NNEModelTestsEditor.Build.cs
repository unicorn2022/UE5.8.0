// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NNEModelTestsEditor : ModuleRules
{
	public NNEModelTestsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "UnrealEd"
            }
        );

        PrivateDependencyModuleNames.AddRange
		(
			new string[]
			{
                "AssetTools",
                "Engine",
				"NNE",
                "NNEModelTests"
            }
		);
	}
}
