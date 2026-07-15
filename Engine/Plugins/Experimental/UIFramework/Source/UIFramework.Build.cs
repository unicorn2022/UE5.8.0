// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UIFramework : ModuleRules
{
	public UIFramework(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"CommonUI",
				"Core",
				"CoreUObject",
				"DeveloperSettings",
				"Engine",
				"EnhancedInput",
				"FieldNotification",
				"LocalizableMessage",
				"ModelViewViewModel",
				"SlateCore",
				"Slate",
				"UMG",
			}
		);

		PublicIncludePathModuleNames.AddRange(
			new string[] {
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"NetCore",
				"CommonInput",
				"GameplayTags",
			}
		);

		SetupIrisSupport(Target);
	}
}
