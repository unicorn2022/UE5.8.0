// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class DataflowMedialSkeleton : ModuleRules
	{
        public DataflowMedialSkeleton(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"GeometryAlgorithms"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[] 
				{
				}
			);
		}
	}
}
