// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class DerivedDataBuildController : ModuleRules
{
	public DerivedDataBuildController(ReadOnlyTargetRules TargetRules) : base(TargetRules)
	{
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"DerivedDataCache",
			"DistributedBuildInterface",
		});
	}
}
