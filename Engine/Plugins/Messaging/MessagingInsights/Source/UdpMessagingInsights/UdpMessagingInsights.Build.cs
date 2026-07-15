// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UdpMessagingInsights : ModuleRules
	{
		public UdpMessagingInsights(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"ApplicationCore",
				"Core",
				"CoreUObject",
				"Messaging",
				"Networking",
				"Slate",
				"SlateCore",
				"TraceAnalysis",
				"TraceLog",
				"TraceInsights",
				"TraceInsightsCore",
				"TraceServices",
				"UdpMessaging",
			});

			if (Target.bCompileAgainstEngine)
			{
				PrivateDependencyModuleNames.AddRange(new string[]
				{
					"Engine",
				});
			}
		}
	}
}
