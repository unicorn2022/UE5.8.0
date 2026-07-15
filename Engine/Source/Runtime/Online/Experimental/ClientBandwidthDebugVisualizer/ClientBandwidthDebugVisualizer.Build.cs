// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ClientBandwidthDebugVisualizer : ModuleRules
{
	public ClientBandwidthDebugVisualizer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Error;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"CoreUObject",
			"Engine",
			"BandwidthDebugDelegates",
			"EngineSettings",
		});
	}
}
