// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanAnimationSerializationEditor : ModuleRules
{
	public MetaHumanAnimationSerializationEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[] {
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Engine",
			"Core",
			"MetaHumanAnimationSerialization",
		});
	}
}
