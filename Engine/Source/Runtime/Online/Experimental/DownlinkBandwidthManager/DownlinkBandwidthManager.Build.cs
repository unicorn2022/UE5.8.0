// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DownlinkBandwidthManager : ModuleRules
{
	public DownlinkBandwidthManager(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Error;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"CoreUObject",
			"BandwidthMeasurementTool",
			"BandwidthDebugDelegates",
		});
	}
}
