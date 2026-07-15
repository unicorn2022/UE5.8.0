// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;
using Microsoft.Extensions.Logging;

public class GRDK : ModuleRules
{
	public GRDK(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		bool bHasGRDK = false;

		(string sdkDir, string sdkRoot) = GetSDKDirAndRoot(Logger, Target);

		if (sdkDir != null)
		{
			ExtraRootPath = ("GSDK", sdkRoot);
			if (IsLegacyFolderStructure())
			{
				PublicSystemIncludePaths.Add(Path.Combine(sdkDir, @"GRDK\GameKit\Include"));
				PublicAdditionalLibraries.Add(Path.Combine(sdkDir, @"GRDK\GameKit\Lib\amd64\xgameruntime.lib"));
			}
			else
			{
				PublicSystemIncludePaths.Add(GetPlatformIncludeDirectory(Target));
				PublicAdditionalLibraries.Add(Path.Combine(GetPlatformLibDirectory(Target), "xgameruntime.lib"));
			}

			bHasGRDK = true;
		}
		else if (!IsTargetArchitectureValid(Target))
		{
			// give a more detailed reason why the GDK is unavailable
			if (GetGDKEdition() >= 260400 && IsLegacyFolderStructure())
			{
				Logger.LogInformation("NOTE: The new cross-platform GDK folder layout is required to use the GDK with ARM64. The legacy layout doesn't include ARM64 support.");
			}
			else
			{
				Logger.LogInformation("NOTE: April 2026 GDK or higher is required to use the GDK runtime with ARM64. Current GDK is {0}", GetGDKEdition());
			}
		}

		PublicDefinitions.Add($"WITH_GRDK={(bHasGRDK ? "1" : "0")}");
		PublicDefinitions.Add($"WITH_LEGACY_GDK_FOLDER_STRUCTURE={(bHasGRDK && IsLegacyFolderStructure() ? "1" : "0")}" );
	}

	public static bool IsTargetArchitectureValid(ReadOnlyTargetRules Target)
	{
		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			return GDKExports.IsArchitectureSupported(Target.Architecture);
		}

		return true;
	}

	public static (string, string) GetSDKDirAndRoot(ILogger Logger, ReadOnlyTargetRules Target)
	{
		if (IsTargetArchitectureValid(Target))
		{
			if (IsGDKEditionValid())
			{
				string CurrentGDKDir = GetCurrentGDKDir();

				bool bHasGRDKFile = IsLegacyFolderStructure()
					? File.Exists(Path.Combine(CurrentGDKDir, @"GRDK\grdk.ini"))
					: File.Exists(Path.Combine(GetPlatformIncludeDirectory(Target), "grdk.h"));
				if (!bHasGRDKFile)
				{
					Logger.LogTrace("GDK not installed or not found - GRDK will not be available");
				}
				else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows) &&
					(!Version.TryParse(Target.WindowsPlatform.WindowsSdkVersion, out Version WindowsSdkVersion) || WindowsSdkVersion.Build < 19041))
				{
					Logger.LogTrace("WindowsPlatform.WindowsSdkVersion should be at least 10.0.19041.0 - GRDK will not be available");
				}
				else
				{
					return (CurrentGDKDir, GetGDKRoot());
				}
			}
			else
			{
				Logger.LogTrace("A valid GDK edition is not available - GRDK will not be available");
			}
		}
		else
		{
			Logger.LogTrace("The GDK does not support this architecture/platform - GRDK will not be available");
		}
		return (null, null);
	}

	public static bool IsValid(ReadOnlyTargetRules Target)
	{
		return IsGDKEditionValid() && IsTargetArchitectureValid(Target);
	}
	public static bool IsGDKEditionValid()
	{
		return GDKExports.IsGRDKEditionValid();
	}

	public static int GetGDKEdition()
	{
		return GDKExports.GetGDKVersionNumber() ?? 0;
	}

	public static string GetGDKRoot()
	{
		return GDKExports.GetGSDKRoot();
	}

	public static string GetCurrentGDKDir()
	{
		return GDKExports.GetCurrentGSDKDir();
	}

	public static string GetPlatformIncludeDirectory(ReadOnlyTargetRules Target)
	{
		if (IsLegacyFolderStructure())
		{
			throw new BuildException("This function is for modern GDK folder structure only - calling code needs to check via IsLegacyFolderStructure");
		}

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			return Path.Combine(GDKExports.GetCurrentGSDKDir(), "windows", "include");
		}
		else
		{
			return Path.Combine(GDKExports.GetCurrentGSDKDir(), "xbox", "include"); 
		}
	}

	public static string GetPlatformLibDirectory(ReadOnlyTargetRules Target)
	{
		if (IsLegacyFolderStructure())
		{
			throw new BuildException("This function is for modern GDK folder structure only - calling code needs to check via IsLegacyFolderStructure");
		}

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			return Path.Combine(GDKExports.GetCurrentGSDKDir(), "windows", "lib", Target.Architecture.WindowsLibDir);
		}
		else
		{
			return Path.Combine(GDKExports.GetCurrentGSDKDir(), "xbox", "lib", "x64"); 
		}
	}

	public static string GetPlatformBinDirectory(ReadOnlyTargetRules Target)
	{
		if (IsLegacyFolderStructure())
		{
			throw new BuildException("This function is for modern GDK folder structure only - calling code needs to check via IsLegacyFolderStructure");
		}

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			return Path.Combine(GDKExports.GetCurrentGSDKDir(), "windows", "bin", Target.Architecture.WindowsLibDir);
		}
		else
		{
			return Path.Combine(GDKExports.GetCurrentGSDKDir(), "xbox", "bin", "x64");
		}
	}
	public static bool IsLegacyFolderStructure()
	{
		return GDKExports.IsLegacyFolderStructure();
	}

	
	public static string GetLegacyExtensionDirectory( string ExtensionName, bool bIsForRedist = false )
	{
		return GDKExports.GetExtensionDirectory( ExtensionName, bIsForRedist );
	}

	public static void AddLegacyExtensionDependency( ReadOnlyTargetRules Target, ModuleRules Module, string ExtensionDirectoryName, string ExtensionFileName, bool bWithLib = true, bool bWithDLL = true, string ExtraLibSubPath = null)
	{
		if (!IsLegacyFolderStructure())
		{
			throw new BuildException("Modern GDK folder structure does not use Extensions - calling code needs to check via IsLegacyFolderStructure");
		}

		string ExtensionDir = GetLegacyExtensionDirectory(ExtensionDirectoryName, false);
		if (!Directory.Exists(ExtensionDir))
		{
			throw new BuildException($"Cannot find {ExtensionDirectoryName} extension");
		}

		string LibDir;
		if (GetGDKEdition() >= 241000)
		{
			LibDir = Path.Combine(ExtensionDir, "Lib", "x64", ExtraLibSubPath ?? "");
		}
		else
		{					
			LibDir = Path.Combine(ExtensionDir, "Lib", ExtraLibSubPath ?? "");
		}

		string RedistDir = GetLegacyExtensionDirectory(ExtensionDirectoryName, true);
		AddDependency( Target, Module, ExtensionFileName, bWithLib, bWithDLL, BinOverrideDir:RedistDir, LibOverrideDir:LibDir );

		Module.PublicSystemIncludePaths.Add(Path.Combine(ExtensionDir, "Include"));
	}

	public static void AddDependency( ReadOnlyTargetRules Target, ModuleRules Module, string Name, bool bWithLib = true, bool bWithDLL = true, string BinOverrideDir = null, string LibOverrideDir = null, string IncludeSubPath = null )
	{
		string LibDirectory = (LibOverrideDir ?? GetPlatformLibDirectory(Target));
		if (bWithLib)
		{
			string LibName = Name + ".lib";
			Module.PublicSystemLibraries.Add(Path.Combine(LibDirectory, LibName));
		}

		string BinDirectory = BinOverrideDir ?? GetPlatformBinDirectory(Target);
		if (bWithDLL && Directory.Exists(BinDirectory))
		{
			string ArchPath = (Target.Architecture == UnrealArch.Arm64) ? "arm64/" : "";

			string DLLName = Name + ".dll";
			Module.RuntimeDependencies.Add("$(TargetOutputDir)/" + ArchPath + DLLName, Path.Combine(BinDirectory, DLLName), StagedFileType.SystemNonUFS);

			if (Target.Type != TargetType.Editor) // prevent duplicate file errors when making installed build
			{
				string PDBName = Name + ".pdb";
				Module.RuntimeDependencies.Add("$(TargetOutputDir)/" + ArchPath + PDBName, Path.Combine(BinDirectory, PDBName), StagedFileType.DebugNonUFS);
			}

			Module.PublicDelayLoadDLLs.Add(DLLName);
		}

		if (IncludeSubPath != null)
		{
			Module.PublicSystemIncludePaths.Add(Path.Combine( GetPlatformIncludeDirectory(Target), IncludeSubPath));
		}
	}
}

