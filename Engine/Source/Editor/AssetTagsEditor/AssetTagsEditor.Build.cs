// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedTargetTypes(TargetType.Editor, TargetType.Program)]
public class AssetTagsEditor : ModuleRules
{
	public AssetTagsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
            new string[] {
                "Core",
				"CoreUObject",
				"Slate",
				"SlateCore",
				
			}
        );
	}
}
