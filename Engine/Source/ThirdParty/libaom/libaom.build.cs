// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class LibAom : ModuleRules
{
	protected virtual string LibAomVersion { get { return "v3.13.1"; } }
	protected virtual string RootDirectory { get { return Target.UEThirdPartySourceDirectory; } }

	protected virtual string LibAomIncludePath { get { return Path.Combine(RootDirectory, "libaom", LibAomVersion, "include"); } }
	protected virtual string LibAomLibraryPath { get { return Path.Combine(RootDirectory, "libaom", LibAomVersion, "lib"); } }

	public LibAom(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

        bool bHaveLibAom = false;

		if (Target.Platform == UnrealTargetPlatform.Win64 && Target.Architecture != UnrealArch.Arm64)
		{
            bHaveLibAom = true;
			string Config = "Release";
			string PlatformSubdir = Target.Architecture == UnrealArch.Arm64 ? "WinArm64" : "Win64";
			PublicAdditionalLibraries.Add(Path.Combine(LibAomLibraryPath, PlatformSubdir, Config, "aom.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibAomLibraryPath, PlatformSubdir, Config, "aom_version.lib"));
			PublicSystemIncludePaths.Add(LibAomIncludePath);
		}

        if (bHaveLibAom)
        {
			PublicDefinitions.Add("WITH_LIBAOM_AV1_DECODER=1");
        }
        else
        {
			PublicDefinitions.Add("WITH_LIBAOM_AV1_DECODER=0");
        }

	}
}
