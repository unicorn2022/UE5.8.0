// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MassCharacterTrajectory : ModuleRules
	{
		public MassCharacterTrajectory(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"Engine",
					"MassCore",
					"MassEntity",
					"MassCommon",
					"MassMovement",
					"PoseSearch",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"MassSpawner",
				}
			);
		}
	}
}
