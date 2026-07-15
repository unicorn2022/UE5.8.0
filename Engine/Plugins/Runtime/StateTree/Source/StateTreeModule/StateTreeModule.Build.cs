// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class StateTreeModuleBase : ModuleRules
	{
		public StateTreeModuleBase(ReadOnlyTargetRules Target) : base(Target)
		{ }

		/// <summary>
		/// Returns true if the StateTree trace recording is supported by the provided target
		/// </summary>
		protected bool IsStateTreeTraceRecordingSupported(ReadOnlyTargetRules target)
		{
			// Allow debugger traces on all non-shipping targets and shipping editors (UEFN)
			return (Target.Configuration != UnrealTargetConfiguration.Shipping || Target.bBuildEditor);
		}

		/// <summary>
		/// Returns true if the StateTreeDebugger (trace analysis) is supported by the provided target
		/// </summary>
		protected bool IsStateTreeDebuggerSupported(ReadOnlyTargetRules target)
		{
			// Allow debugger trace analysis on non-game targets on desktop platforms
			// (consoles don't have TraceAnalysis support and RewindDebuggerInterface is only available on Editor and Program targets)
			return IsStateTreeTraceRecordingSupported(target)
				&& (Target.Platform.IsInGroup(UnrealPlatformGroup.Desktop)
				&& (Target.Type == TargetType.Editor || Target.Type == TargetType.Program));
		}
		
		/// <summary>
		/// Setup this module for StateTree debugging support (i.e., Trace recording and Trace analysis)
		/// </summary>
		public void SetupStateTreeDebuggingSupport(ReadOnlyTargetRules target)
		{
			if (IsStateTreeTraceRecordingSupported(target))
			{
				PublicDefinitions.Add("WITH_STATETREE_TRACE=1");
			}
			else
			{
				PublicDefinitions.Add("WITH_STATETREE_TRACE=0");
			}

			if (IsStateTreeDebuggerSupported(target))
			{
				PublicDefinitions.Add("WITH_STATETREE_TRACE_DEBUGGER=1");
			}
			else
			{
				PublicDefinitions.Add("WITH_STATETREE_TRACE_DEBUGGER=0");
			}
		}
	}

	public class StateTreeModule : StateTreeModuleBase
	{
		public StateTreeModule(ReadOnlyTargetRules Target) : base(Target)
		{
			CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Warning;

			bAllowUETypesInNamespaces = true;

			PublicIncludePaths.AddRange(
				new string[] {
				}
			);

			PublicDependencyModuleNames.AddRange(
				new [] {
					"Core",
					"CoreUObject",
					"DeveloperSettings",
					"Engine",
					"AIModule", //@TODO Move to Private dependency when AITypes.h deprection is removed.
					"GameplayTags",
					"PropertyBindingUtils"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new [] {
					"PropertyPath",
				}
			);

			CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Error;

			if (Target.bBuildEditor)
			{
				PublicDependencyModuleNames.AddRange(
					new [] {
						"UnrealEd",
						"BlueprintGraph",
					}
				);
				PrivateDependencyModuleNames.AddRange(
					new [] {
						"StructUtilsEditor",
						"EditorSubsystem",
						"EditorFramework"
					}
				);
			}

			SetupStateTreeDebuggingSupport(Target);

			if (IsStateTreeTraceRecordingSupported(Target))
			{
				PublicDependencyModuleNames.AddRange(
					new []
					{
						"TraceLog"
					}
				);
			}

			if (IsStateTreeDebuggerSupported(Target))
			{
				PublicDependencyModuleNames.AddRange(
					new[]
					{
							"TraceServices",
							"TraceAnalysis"
					});

				PrivateDependencyModuleNames.AddRange(
					new[]
					{
							"RewindDebuggerInterface"
					});
			}
		}
	}
}
