// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class BandwidthMeasurementTool : ModuleRules
{
	public BandwidthMeasurementTool(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Error;

		PublicDependencyModuleNames.AddRange(new string[]{
			"Core",
			});

		PrivateDependencyModuleNames.AddRange(new string[]{
			"CoreUObject",
			"HTTP",
			"BandwidthDebugDelegates",
			});
	}
}