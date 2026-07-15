// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class LibVpx : ModuleRules
{
	protected virtual System.Version LibVPXVersionNumber => new System.Version(1, 14, 1);
	protected virtual string LibVpxVersion { get { return $"libvpx-{LibVPXVersionNumber.ToString(3)}"; } }
	protected virtual string RootDirectory { get { return Target.UEThirdPartySourceDirectory; } }

	protected virtual string LibvpxIncludePath { get { return Path.Combine(RootDirectory, "libvpx", LibVpxVersion, "include"); } }
	protected virtual string LibvpxLibraryPath { get { return Path.Combine(RootDirectory, "libvpx", LibVpxVersion, "lib"); } }

	public LibVpx(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		string LibraryPath = LibvpxLibraryPath + "/";
		string PlatformSubdir = Target.Platform.ToString();

		if (LibVPXVersionNumber >= new System.Version(1, 15, 1))
		{
			string Config = "Release";
			PlatformSubdir = Target.Architecture == UnrealArch.Arm64 ? "Arm64" : "x86_64";
			string libraryName = "libvpx";

			// ------------------------------------------------------
			// -------------- DESKTOP PLATFORMS
			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibvpxLibraryPath, "Win", PlatformSubdir, Config, $"{libraryName}.lib"));
			}
			else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibvpxLibraryPath, "Linux", PlatformSubdir, Config, $"{libraryName}.a"));
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibvpxLibraryPath, "Mac", Config, $"{libraryName}.a"));
			}

			// ------------------------------------------------------
			// -------------- MOBILE PLATFORMS
			else if (Target.Platform == UnrealTargetPlatform.Android)
			{
				string[] Architectures = [
					"Arm64",
					"x86_64",
				];

				foreach (string Architecture in Architectures)
				{
					PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Android", Architecture, Config, $"{libraryName}.a"));
				}
			}
			else if (Target.Platform == UnrealTargetPlatform.IOS)
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "IOS", Config, $"{libraryName}.a"));
			}
			PublicSystemIncludePaths.Add(LibvpxIncludePath);
		}
		// Old format for Version 1.14.1
		else
		{
			string Config = Target.Configuration == UnrealTargetConfiguration.Debug ? "Debug" : "Release";

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				// TODO (william.belcher): Do we really need to ship debug binaries?
				Config = "Release";
				PlatformSubdir = Target.Architecture == UnrealArch.Arm64 ? "WinArm64" : "Win64";
				PublicAdditionalLibraries.Add(Path.Combine(LibvpxLibraryPath, PlatformSubdir, Config, "libvpx.lib"));
			}
			else if (Target.Platform == UnrealTargetPlatform.Android)
			{
				string[] Architectures = [
					"arm64-v8a",
					"x86_64",
				];

				Config = "Release";
				foreach (string Architecture in Architectures)
				{
					PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, PlatformSubdir, Architecture, Config, "libvpx.a"));
				}
			}
			else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibvpxLibraryPath, "Unix", Target.Architecture.LinuxName, ((Target.LinkType == TargetLinkType.Monolithic) ? "libvpx.a" : "libvpx_fPIC.a")));
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibvpxLibraryPath, "Mac", ((Target.LinkType == TargetLinkType.Monolithic) ? "libvpx.a" : "libvpx_fPIC.a")));
			}
			else if (Target.Platform == UnrealTargetPlatform.IOS)
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibvpxLibraryPath, "IOS", "libvpx.a"));
			}
			PublicSystemIncludePaths.Add(LibvpxIncludePath);
		}
	}
}
