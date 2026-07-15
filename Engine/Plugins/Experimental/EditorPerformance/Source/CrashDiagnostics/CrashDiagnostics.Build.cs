// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CrashDiagnostics : ModuleRules
{
	public CrashDiagnostics(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"TypedElementFramework",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"CrashReportCore",
				"InputCore",
				"Slate",
				"SlateCore",
				"TedsQueryStack",
				"TedsTableViewer",
				"ToolMenus",
				"ToolWidgets",
				"UnrealEd",
				"XmlParser",
			}
		);
	}
}