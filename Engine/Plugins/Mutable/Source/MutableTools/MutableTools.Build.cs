// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
    public class MutableTools : ModuleRules
    {
        public MutableTools(ReadOnlyTargetRules Target) : base(Target)
		{
			ShortName = "MuT";

			DefaultBuildSettings = BuildSettingsVersion.Latest;
			IWYUSupport = IWYUSupport.KeepAsIsForNow;

			PublicDependencyModuleNames.AddRange(
                new string[] {
					"MutableRuntime",
					"Core",
					"CoreUObject",
					"GeometryCore",
					"ImageCore",
					"TextureCompressor",
					"TextureBuildUtilities",
					"Engine",
				}
			);
		}
	}
}
