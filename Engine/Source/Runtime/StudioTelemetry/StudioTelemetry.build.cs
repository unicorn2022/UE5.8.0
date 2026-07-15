// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class StudioTelemetry : ModuleRules
{
	public StudioTelemetry(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"HTTP",
                "BuildSettings",
                "Analytics",
				"AnalyticsCSV",
				"AnalyticsET",
				"AnalyticsHorde",
				"AnalyticsLog",
				"TelemetryUtils"
			}
		);
	}
}
