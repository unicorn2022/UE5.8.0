// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UAFMass : ModuleRules
{
	public UAFMass(ReadOnlyTargetRules Target) : base(Target)
	{

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"UAF",
				"MassCommon"
			}
			);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"RigVM",
				"Engine",
				"MassSpawner",
				"MassSimulation",
				"MassCore",
				"MassEntity",
				"MassCharacterTrajectory"
			}
			);

		SetupGameplayDebuggerSupport(Target);
	}
}
