// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class WorldStreamingInsights : ModuleRules
	{
		public WorldStreamingInsights(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"ApplicationCore",
					"Core",
					"CoreUObject",
					"InputCore",
					"Slate",
					"SlateCore",
					"TraceAnalysis",
					"TraceInsights",
					"TraceInsightsCore",
					"TraceServices"
				}
			);
		}
	}
}