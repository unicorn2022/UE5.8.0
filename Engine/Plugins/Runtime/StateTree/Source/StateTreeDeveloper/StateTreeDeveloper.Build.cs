// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

namespace UnrealBuildTool.Rules
{
	public class StateTreeDeveloper : StateTreeModuleBase
	{
		public StateTreeDeveloper(ReadOnlyTargetRules Target) : base(Target)
		{
			CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Warning;

			bAllowUETypesInNamespaces = true;

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"SlateCore",
				}
			);
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"CoreUObject",
					"InputCore",
					"Slate",
					"StateTreeModule",
				}
			);

			SetupStateTreeDebuggingSupport(Target);

			if (IsStateTreeTraceRecordingSupported(Target))
			{
				PublicDependencyModuleNames.AddRange(
					new []
					{
						"RewindDebuggerRuntimeInterface",
						"TraceLog"
					}
				);
			}
		}
	}
}