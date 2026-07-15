// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class RewindDebugger : ModuleRules
	{
		public RewindDebugger(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ActorPickerMode",
				"Core",
				"CoreUObject",
				"DeveloperSettings",
				"Engine",
				"GameplayInsights",
				"InputCore",
				"LevelSequence", // required for hard coded level sequence special case in ObjectTrack
				"RewindDebuggerInterface",
				"RewindDebuggerRuntime",
				"Slate",
				"SlateCore",
				"Sockets",
				"StatusBar",
				"ToolMenus",
				"ToolWidgets",
				"TraceAnalysis",
				"TraceBasedDebuggers",
				"TraceBasedDebuggersAnalysis",
				"TraceInsights",
				"TraceLog",
				"TraceServices",
				"UnrealEd",
			});
		}
	}
}

