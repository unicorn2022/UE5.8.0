// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ShaderAuditCore : ModuleRules
{
	public ShaderAuditCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"ApplicationCore",
				"DesktopPlatform",
				"InputCore",
				"RenderCore",
				"RHI",
				"Slate",
				"SlateCore",
				"ToolWidgets",
				"TreeMap",
				"xxhash",
			}
		);
	}
}
