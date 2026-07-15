// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedPlatforms(UnrealPlatformClass.All)]
 public class ChaosPhysicsTestsTarget : ChaosTestHarnessTarget
 {
     public ChaosPhysicsTestsTarget(TargetInfo Target) : base(Target)
     {
		TestTargetRules.bTestsRequireEngine = true;
     }
 }