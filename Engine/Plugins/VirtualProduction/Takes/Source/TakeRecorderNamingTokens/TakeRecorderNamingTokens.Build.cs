// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TakeRecorderNamingTokens : ModuleRules
{
	public TakeRecorderNamingTokens(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Engine",
				"TakeRecorder",
				"TakesCore",
				"NamingTokens"
			}
		);

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"UnrealEd",
					"MovieSceneTools"
				}
			);
		}
	}
}
