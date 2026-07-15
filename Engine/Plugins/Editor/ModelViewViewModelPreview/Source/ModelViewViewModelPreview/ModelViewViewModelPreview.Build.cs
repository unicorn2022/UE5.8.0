// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ModelViewViewModelPreview : ModuleRules
{
	public ModelViewViewModelPreview(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.AddRange(
			new string[]
			{
				"UMGWidgetPreview",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AdvancedWidgets",
				"ApplicationCore",
				"Core",
				"CoreUObject",
				"DesktopPlatform",
				"FieldNotification",
				"Json",
				"InputCore",
				"MessageLog",
				"ModelViewViewModel",
				"ModelViewViewModelBlueprint",
				"Projects",
				"Slate",
				"SlateCore",
				"UMG",
				"UMGWidgetPreview",
			});
	}
}
