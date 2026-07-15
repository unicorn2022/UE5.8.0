// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class LibDav1d : ModuleRules
{
	protected virtual string LibraryVersion { get { return "v1.5.1"; } }
	protected virtual string RootDirectory { get { return Target.UEThirdPartySourceDirectory; } }

	protected virtual string LibAomIncludePath { get { return Path.Combine(RootDirectory, "libdav1d", LibraryVersion, "include"); } }
	protected virtual string LibAomLibraryPath { get { return Path.Combine(RootDirectory, "libdav1d", LibraryVersion, "lib"); } }

	public LibDav1d(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

        bool bHaveLibDav1d = false;

        if (Target.Platform == UnrealTargetPlatform.Win64 && Target.Architecture != UnrealArch.Arm64)
		{
            bHaveLibDav1d = true;
			string Config = "Release";
			string PlatformSubdir = Target.Architecture == UnrealArch.Arm64 ? "WinArm64" : "Win64";
			PublicAdditionalLibraries.Add(Path.Combine(LibAomLibraryPath, PlatformSubdir, Config, "libdav1d.a"));
			PublicSystemIncludePaths.Add(LibAomIncludePath);
		}
        else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix) && Target.Architecture != UnrealArch.Arm64)
		{
            bHaveLibDav1d = true;
			string PlatformSubdir = Target.Architecture.LinuxName;
			PublicAdditionalLibraries.Add(Path.Combine(LibAomLibraryPath, "Linux", PlatformSubdir, "libdav1d.a"));
			PublicSystemIncludePaths.Add(LibAomIncludePath);
		}

        if (bHaveLibDav1d)
        {
			PublicDefinitions.Add("WITH_LIBDAV1D_AV1_DECODER=1");
        }
        else
        {
			PublicDefinitions.Add("WITH_LIBDAV1D_AV1_DECODER=0");
        }

	}
}
