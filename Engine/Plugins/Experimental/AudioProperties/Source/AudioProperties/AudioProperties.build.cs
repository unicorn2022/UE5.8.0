// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class AudioProperties : ModuleRules
	{
		public AudioProperties(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"AudioExtensions",
					"AssetRegistry",
					"Core",
					"CoreUObject",
					"TargetPlatform"
				}
			);
		}
	}
}