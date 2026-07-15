// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class VoiceChat : ModuleRules
	{
		public VoiceChat(ReadOnlyTargetRules Target) : base(Target)
		{
			CppCompileWarningSettings.ModuleIncludePathWarningLevel = WarningLevel.Off;

			PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core"
				}
			);
		}
	}
}
