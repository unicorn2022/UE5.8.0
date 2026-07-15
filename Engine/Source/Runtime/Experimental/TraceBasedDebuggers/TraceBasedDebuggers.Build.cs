// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class TraceBasedDebuggers : ModuleRules
	{
		public TraceBasedDebuggers(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Messaging",
					"MessagingCommon",
					"TraceLog"
				}
				);

			bAllowUETypesInNamespaces = true;
			CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Error;

			if (AreTraceBasedDebuggersSupported(Target))
			{
				PublicDefinitions.Add("WITH_TRACE_BASED_DEBUGGERS=1");
			}
			else
			{
				PublicDefinitions.Add("WITH_TRACE_BASED_DEBUGGERS=0");
			}
		}
	}
}
