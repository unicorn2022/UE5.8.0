// Copyright Epic Games, Inc. All Rights Reserved.
using System.IO;
using UnrealBuildTool;
using System;

public class RivermaxLib : ModuleRules
{
	public RivermaxLib(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "include"));

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
		{
			string RivermaxDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Mellanox", "Rivermax");
			string DefaultRivermaxLibDir = Path.Combine(RivermaxDir, "Lib");
			
			//This is required because Rivermax depends on other drivers / dll to be installed for mellanox. We will manually load the dll and gracefully fail instead of 
			//failing to load the module entirely.
			PublicDelayLoadDLLs.Add("rivermax.dll");
			
			//Used during manual loading of the library
			PublicDefinitions.Add("RIVERMAX_LIBRARY_PLATFORM_PATH=" + DefaultRivermaxLibDir.Replace(@"\", "/"));
			PublicDefinitions.Add("RIVERMAX_LIBRARY_NAME=" + "rivermax.dll");
		
			string SDKThirdPartyPath = Path.Combine(Target.UEThirdPartySourceDirectory, "NVIDIA/Rivermax");
			PublicSystemIncludePaths.Add(Path.Combine(SDKThirdPartyPath,"include"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			// UnrealTargetPlatform.Linux is x86_64 only; LinuxArm64 is a separate enum.
			// Rivermax SDK only ships for x86_64 Linux; installs to /usr/lib/x86_64-linux-gnu/ (Debian/Ubuntu package).
			const string RivermaxLibDir = "/usr/lib/x86_64-linux-gnu";
			PublicDefinitions.Add("RIVERMAX_LIBRARY_PLATFORM_PATH=" + RivermaxLibDir);
			PublicDefinitions.Add("RIVERMAX_LIBRARY_NAME=librivermax.so");
			// Do not add a link-time dependency -- Linux has no delay-load equivalent.
			// The library is loaded at runtime via FPlatformProcess::GetDllHandle (dlopen) in
			// RivermaxManager::LoadLibrary, which gracefully handles the library being absent.
		}
	}
}

