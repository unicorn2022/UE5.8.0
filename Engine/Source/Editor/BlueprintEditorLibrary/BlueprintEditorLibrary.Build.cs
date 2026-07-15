// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

[SupportedTargetTypes(TargetType.Editor, TargetType.Program)]
public class BlueprintEditorLibrary : ModuleRules
{
	public BlueprintEditorLibrary(ReadOnlyTargetRules Target) : base(Target)
	{		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"BlueprintGraph"
			}
		);
			
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AnimGraph",
				"CoreUObject",
				"Engine",
				"Json",
				"JsonUtilities",
				"KismetCompiler",
				"UnrealEd",
			}
		);
	}
}
