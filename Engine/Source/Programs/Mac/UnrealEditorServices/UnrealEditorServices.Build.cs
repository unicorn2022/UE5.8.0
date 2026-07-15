// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UnrealEditorServices : ModuleRules
{
	public UnrealEditorServices(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePathModuleNames.Add("Launch");
		bRequiresPlatformSDK = true;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"ApplicationCore",
				"DesktopPlatform",
				"Json",
				"Projects",
			}
		);
	}
}
