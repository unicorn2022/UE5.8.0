// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class TmvMedia : ModuleRules
	{
		public TmvMedia(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"Engine",
					"Media",
					"RHI",
					"TmvMediaShaders",
				});

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"ImageCore",
					"ImageWrapper",
					"Json",
					"JsonUtilities",
					"MediaAssets",
					"MediaUtils",
					"Projects",
					"Renderer",
					"RenderCore",
				});

			// CQTest is a Developer module; only link it when developer/editor tools are being built
			// so that it is never pulled into shipping or game-client binaries.
			if (Target.bBuildEditor && Target.bBuildDeveloperTools)
			{
				PrivateDependencyModuleNames.Add("CQTest");
			}
		}
	}
}
