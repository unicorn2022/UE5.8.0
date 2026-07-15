// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MP4Muxer : ModuleRules
	{
		public MP4Muxer(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"Core",
                "MP4Utilities"
            });
		}
	}
}
