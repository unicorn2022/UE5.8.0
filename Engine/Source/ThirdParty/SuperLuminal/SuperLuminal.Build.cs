// Copyright Epic Games, Inc. All Rights Reserved.
using EpicGames.Core;
using UnrealBuildBase;
using UnrealBuildTool;
using System;
using System.IO;

public class SuperLuminal : ModuleRules
{
	public SuperLuminal(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string GetLibPath(string CrtFolder) => Path.Combine(ModuleDirectory, "Lib", Target.Architecture.WindowsLibDir, CrtFolder, "SuperLuminal.lib");

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) && Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			string SuperluminalInstallDir = OperatingSystem.IsWindows() ? Microsoft.Win32.Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\Superluminal\Performance", "InstallDir", null) as string : null;
			if (String.IsNullOrEmpty(SuperluminalInstallDir))
			{
				SuperluminalInstallDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Superluminal", "Performance");
			}

			string SuperluminalApiDir = Path.Combine(SuperluminalInstallDir, "API");
			string SuperluminalLibDir = Path.Combine(SuperluminalApiDir, "lib", Target.Architecture.WindowsLibDir);

			if (Target.Configuration != UnrealTargetConfiguration.Shipping &&
				File.Exists(Path.Combine(SuperluminalApiDir, "include/Superluminal/PerformanceAPI_capi.h")))
			{
				string PerformanceLib;
				string Crt;
				if (Target.bDebugBuildsActuallyUseDebugCRT == true && Target.Configuration == UnrealTargetConfiguration.Debug)
				{
					PerformanceLib = "PerformanceAPI_MDd.lib";
					Crt = "MDd";
				}
				else
				{
					PerformanceLib = "PerformanceAPI_MD.lib";
					Crt = "MD";
				}

				string LibPath = GetLibPath(Crt);

				if (FileItem.GetItemByPath(LibPath).Exists)
				{
					ExtraRootPath = ("SuperLuminal", SuperluminalApiDir);
					PrivateDefinitions.Add("WITH_SUPERLUMINAL_PROFILER=1");
					PrivateIncludePaths.Add(Path.Combine(SuperluminalApiDir, "include/"));
					PublicAdditionalLibraries.Add(Path.Combine(SuperluminalLibDir, PerformanceLib));
					PublicAdditionalLibraries.Add(LibPath);

					return;
				}
			}

			PrivateDefinitions.Add("WITH_SUPERLUMINAL_PROFILER=0");
		}

		PublicAdditionalLibraries.Add(GetLibPath("Null"));
	}
}
