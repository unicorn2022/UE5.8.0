// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DisplayClusterMonitor : ModuleRules
{
	public DisplayClusterMonitor(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"DisplayCluster",
				"DisplayClusterConfiguration",
				"DisplayClusterShaders",
				"Engine",
				"MediaIOCore",
				"NDIMedia",
				"RenderCore",
				"RHI",
			});

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Messaging",
				"MessagingCommon",
			});

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
