// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LedWallCalibration : ModuleRules
{
	public LedWallCalibration(ReadOnlyTargetRules Target) : base(Target)
	{
		DefaultBuildSettings = BuildSettingsVersion.Latest;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CameraCalibrationCore",
				"Core",
				"CoreUObject",
				"Engine",
				"MeshDescription",
				"OpenCV",
				"OpenCVHelper",
				"StaticMeshDescription",
			}
		);
	}
}
