// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class InterchangeAnalytics : ModuleRules
	{
		public InterchangeAnalytics(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"InterchangeCore",
					"InterchangeCommon",
					"InterchangeEngine",
					"InterchangeFactoryNodes",
					"InterchangeNodes",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"GeometryCache",
					"HairStrandsCore",
					"LevelSequence",
				}
			);
		}
	}
}
