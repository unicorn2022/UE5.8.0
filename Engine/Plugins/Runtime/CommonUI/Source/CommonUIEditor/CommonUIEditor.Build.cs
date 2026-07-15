// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CommonUIEditor : ModuleRules
{
	public CommonUIEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
		new string[]
			{
				"AssetDefinition",
				"Core",
				"CoreUObject",
				"ApplicationCore",
				"Engine",
				"EngineAssetDefinitions",
				"PropertyEditor",
				"InputCore",
				"Slate",
				"UMG",
				"SlateCore",
				"CommonUI",
                "EditorWidgets",	
				"UnrealEd",
				"GameplayTags",
				"GameplayTagsEditor",
				"AssetRegistry",
				"ToolMenus",
			}
        );

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"DataTableEditor",
			}
		);

		PublicIncludePaths.AddRange(
			new string[]
			{
			}
		);
	}
}
