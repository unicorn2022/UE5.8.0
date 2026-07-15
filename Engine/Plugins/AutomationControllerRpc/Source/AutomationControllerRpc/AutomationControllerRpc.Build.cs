// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using UnrealBuildTool;

public class AutomationControllerRpc : ModuleRules
{
	public AutomationControllerRpc(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"ExternalRpcRegistry",
				"Json",
				"HTTP",
				"HTTPServer",
				"SessionServices"
			}
			);

		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateDependencyModuleNames.Add("AutomationController");
		}
	}
}
