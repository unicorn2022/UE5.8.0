// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GeometryCore : ModuleRules
{
	public GeometryCore(ReadOnlyTargetRules Target) : base(Target)
	{
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePathModuleNames.AddRange(
			[
				"AnimationCore",			// For the BoneWeights.h include
				"Engine",					// For GPUSkinPublicDefs.h
			]
		);

		PublicDependencyModuleNames.AddRange(
			[
				"Core"
			]
		);

		PrivateDependencyModuleNames.AddRange(
			[
				"AutoRTFM",
			]
		);

		CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Error;
	}
}
