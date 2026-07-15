// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ShaderAudit : ModuleRules
{
	public ShaderAudit(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"ShaderAuditCore",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"ApplicationCore",
				"AssetRegistry",
				"ContentBrowser",
				"Core",
				"CoreUObject",
				"DesktopPlatform",
				"Engine",
				"EditorSubsystem",
				"InputCore",
				"MaterialValidation",
				"RenderCore",
				"RHI",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"ToolWidgets",
				"TreeMap",
				"UnrealEd",
				"WorkspaceMenuStructure",
			}
        );
    }
}
