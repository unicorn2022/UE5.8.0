// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MassCore : ModuleRules
	{
		public MassCore(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

			CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Warning;

			// Headers live in Public/Mass/ — add to include paths so bare
			// #include "EntityHandle.h" resolves for direct consumers.
			PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"TraceLog",
				}
			);
		}
	}
}
