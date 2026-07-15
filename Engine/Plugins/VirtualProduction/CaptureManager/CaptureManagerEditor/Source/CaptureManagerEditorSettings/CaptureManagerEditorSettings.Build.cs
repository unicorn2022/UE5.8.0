// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using UnrealBuildTool.Rules;

public class CaptureManagerEditorSettings : ModuleRules
{
	public CaptureManagerEditorSettings(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"CaptureUtils"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"DeveloperSettings",
			"CaptureDataConverter",
			"CaptureManagerStyle",
			"ContentBrowser",
			"ContentBrowserData",
			"LiveLinkHubCaptureMessaging",
			"LiveLinkHubExportServer",
			"LiveLinkHubMessaging",
			"LiveLinkInterface",
			"NamingTokens",
			"PropertyEditor",
			"Slate",
			"SlateCore",
			"Settings",
			"ToolMenus",
			"UnrealEd"
		});

		ShortName = "CapManEdSet";
	}
}
