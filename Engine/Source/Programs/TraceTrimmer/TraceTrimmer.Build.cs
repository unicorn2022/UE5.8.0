// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TraceTrimmer : ModuleRules
{
	public TraceTrimmer(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePathModuleNames.Add("Launch");
		PrivateDependencyModuleNames.Add("Core");
		PrivateDependencyModuleNames.Add("Projects");
		PrivateDependencyModuleNames.Add("TraceAnalysis");
		PrivateDependencyModuleNames.Add("TraceServices");
	}
}
