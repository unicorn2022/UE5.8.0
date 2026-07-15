// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedTargetTypes(TargetType.Editor, TargetType.Program)]
public class StorageServerWidgets : ModuleRules
{
	public StorageServerWidgets(ReadOnlyTargetRules Target)
		 : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"ApplicationCore",
				"Core",
				"CoreUObject",
				"DesktopPlatform",
				"InputCore",
				"Json",
				"Projects",	
				"Slate",
				"SlateCore",
				"ToolWidgets",
				"Zen"
			});

		CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Error;
	}
}
