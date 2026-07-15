// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedTargetTypes(TargetType.Editor, TargetType.Program)]
public class RewindDebuggerInterface : ModuleRules
{
	public RewindDebuggerInterface(ReadOnlyTargetRules Target) : base(Target)
	{

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"SlateCore",
			}
		);
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Slate",
				"ToolWidgets",
			}
		);
	}
}
