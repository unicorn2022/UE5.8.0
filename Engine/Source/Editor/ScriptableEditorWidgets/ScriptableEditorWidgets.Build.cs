// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedTargetTypes(TargetType.Editor, TargetType.Program)]
public class ScriptableEditorWidgets : ModuleRules
{
	public ScriptableEditorWidgets(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string [] {
				"Core",
				"Engine",
				"Slate",
				"SlateCore",
			}
		);
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"CoreUObject",
				"UMG",		
			}
		);
	}
}
