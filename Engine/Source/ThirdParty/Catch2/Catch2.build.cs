// Copyright Epic Games, Inc. All Rights Reserved.

#define WITH_CATCH2

using UnrealBuildTool;
using System.IO;
using UnrealBuildBase;
using System.Linq;

public class Catch2 : ModuleRules
{
	private static string Version = "v3.11.0";

	protected virtual bool IsDebugConfig
	{
		get
		{
			return Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT;
		}
	}

	public static string Catch2Version
	{
		get
		{
			return Version;
		}
	}

	/// <summary>
	/// Select a sepecific Catch2 version. Default version is always latest.
	/// </summary>
	/// <param name="WithVersion"></param>
	public static void OverrideCatch2Version(string WithVersion)
	{
		Version = WithVersion;
	}

	/// <summary>
	/// Library name can vary with platform.
	/// For NDA platforms inherit from this module and override this property to set a different library name.
	/// </summary>
	public virtual string LibName
	{
		get
		{
			if (Target.Platform.IsInGroup(UnrealPlatformGroup.Microsoft))
			{
				return string.Format("Catch2{0}.lib", IsDebugConfig ? "d" : string.Empty);
			}
			return string.Format("libCatch2{0}.a", IsDebugConfig ? "d" : string.Empty);
		}
	}

	bool IsPlatformExtension()
	{
		return !(Target.Platform.IsInGroup(UnrealPlatformGroup.Desktop) ||
				 Target.Platform == UnrealTargetPlatform.Android ||
				 Target.Platform == UnrealTargetPlatform.IOS);
	}

	public virtual string Catch2Root
	{
		get
		{
			if (IsPlatformExtension())
			{
				return Path.Combine(Unreal.EngineDirectory.FullName, "Platforms", Target.Platform.ToString(), "Source", "ThirdParty", "Catch2");
			}
			else
			{
				return Path.Combine(Unreal.EngineDirectory.FullName, "Source", "ThirdParty", "Catch2");
			}
		}
	}

	public virtual string RelativeBaseLibPath
	{
		get
		{
			string RelativeLibPath = IsPlatformExtension() ? string.Empty : Target.Platform.ToString();
			string Arch = string.Empty;
			string Variation = string.Empty;
			if (Target.Platform == UnrealTargetPlatform.Android)
			{
				Arch = "arm64";
			}
			else if (Target.Platform == UnrealTargetPlatform.Linux)
			{
				Arch = "x86_64-unknown-linux-gnu";
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac && Version == "v3.11.0")
			{
				RelativeLibPath = UnrealTargetPlatform.Mac.ToString();
				if (Target.Architecture == UnrealArch.Arm64)
				{
					Arch = "arm64";
				}
				else if (Target.Architecture == UnrealArch.X64)
				{
					Arch = "x86_64";
				}
			}
			else if (Target.Platform == UnrealTargetPlatform.LinuxArm64)
			{
				RelativeLibPath = UnrealTargetPlatform.Linux.ToString();
				Arch = "aarch64-unknown-linux-gnueabi";
			}
			else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
			{
				RelativeLibPath = "Win64";
				if (Target.Architecture == UnrealArch.Arm64)
				{
					Arch = "arm64";
				}
				else
				{
					Arch = "x64";
				}
				if (Target.WindowsPlatform.ToolChain >= WindowsCompiler.VisualStudio2022)
				{
					Variation = "VS2022";
				}
			}
			if (!string.IsNullOrEmpty(Arch))
			{
				RelativeLibPath = Path.Combine(RelativeLibPath, Arch);
			}
			if (!string.IsNullOrEmpty(Variation))
			{
				RelativeLibPath = Path.Combine(RelativeLibPath, Variation);
			}
			return RelativeLibPath;
		}
	}

	public Catch2(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string RelativeLibPath = Path.Combine(RelativeBaseLibPath, IsDebugConfig ? "debug" : "release", LibName);

		PublicAdditionalLibraries.Add(Path.Combine(Catch2Root, Version, "lib", RelativeLibPath));
		PublicSystemIncludePaths.Add(Path.Combine(Unreal.EngineDirectory.FullName, "Source", "ThirdParty", "Catch2", Version, "src"));
	}
}
