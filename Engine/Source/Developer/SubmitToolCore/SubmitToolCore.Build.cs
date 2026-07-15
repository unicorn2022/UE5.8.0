// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
public class SubmitToolCore : ModuleRules
{
	public SubmitToolCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePathModuleNames.AddRange(
			new string[] {
				"TargetPlatform"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"AppFramework",
				"Core",
				"CoreUObject",
				"Json",
				"OutputLog"
			}
		);
	}
}
