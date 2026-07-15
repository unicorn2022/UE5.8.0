// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedTargetTypes(TargetType.Editor, TargetType.Program)]
public class AnimationBlueprintLibrary : ModuleRules
{
	public AnimationBlueprintLibrary(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"TimeManagement"
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"AnimGraph",
				"Engine",
				"BlueprintGraph",
				"UnrealEd",
			}
		);
	}
}
