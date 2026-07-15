// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class RewindDebuggerVLogRuntime : ModuleRules
	{
		public RewindDebuggerVLogRuntime(ReadOnlyTargetRules Target) : base(Target)
		{
			bAllowUETypesInNamespaces = true;

			PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"RewindDebuggerRuntimeInterface",
				"TraceBasedDebuggers",
				"TraceLog"
			});
		}
	}
}

