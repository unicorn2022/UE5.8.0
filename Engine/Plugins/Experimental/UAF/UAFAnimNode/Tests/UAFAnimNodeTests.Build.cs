// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UAFAnimNodeTests : TestModuleRules
{
	public UAFAnimNodeTests(ReadOnlyTargetRules Target) : base(Target, true)
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
				"UAFAnimNode"
			}			
		);
	}
}