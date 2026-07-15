// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UAFAnimGraphTests : TestModuleRules
{
	public UAFAnimGraphTests(ReadOnlyTargetRules Target) : base(Target, true)
	{
		bAllowUETypesInNamespaces = true;
		
		PrivateIncludePaths.AddRange(
			// Any private include paths
			new string[] {
			}	
		);
		
		PrivateDependencyModuleNames.AddRange(
			// Any private dependencies to link against
			new string[] {
				"Core",
				"CoreUObject",
				"RigVM",
				"Engine",
				"UAF",
				"HierarchyTableRuntime",
				"HierarchyTableAnimationRuntime",
				"TraceLog",				
				"UAFAnimGraph"
			}			
		);
	}
}