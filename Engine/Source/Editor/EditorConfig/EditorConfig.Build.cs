// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	[SupportedTargetTypes(TargetType.Editor, TargetType.Program)]
	public class EditorConfig : ModuleRules
	{
		public EditorConfig(ReadOnlyTargetRules Target) : base(Target)
		{
			// Enable truncation warnings in this module
			CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Warning;

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"EditorSubsystem",
					"Engine",
					"Json",
					"UnrealEd",
				} );
		}
	}
}
