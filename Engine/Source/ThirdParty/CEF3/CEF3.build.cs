// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System.Collections.Generic;
using EpicGames.Core;
using UnrealBuildBase;

public class CEF3 : ModuleRules
{
	public CEF3(ReadOnlyTargetRules Target) : base(Target)
	{
		/** Mark the current version of the library */
		string CEFVersion = "";
		string CEFPlatform = "";
		bool bUseExperimentalVersion = false;

		string RuntimePlatform = Target.Platform.ToString();

		Type = ModuleType.External;
		IWYUSupport = IWYUSupport.None;

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			if (Target.Architecture == UnrealArch.Arm64)
			{
				CEFVersion = bUseExperimentalVersion ? "147.1.5+g68529a5+chromium-147.0.7727.49" : "128.4.13+ge76af7e+chromium-128.0.6613.138";
				CEFPlatform = "windowsarm64";
				RuntimePlatform = "WinArm64";
			}
			else
			{
				CEFVersion = bUseExperimentalVersion ? "147.1.5+g68529a5+chromium-147.0.7727.49" : "128.4.13+ge76af7e+chromium-128.0.6613.138+v2";
				CEFPlatform = "windows64";
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			CEFVersion = "128.4.13+ge76af7e+chromium-128.0.6613.138";
			// the wrapper.la that is in macosarm64 is universal, so we always point to this one for the lib
			CEFPlatform = "macos_universal";
		}
		else if(Target.Platform == UnrealTargetPlatform.Linux)
		{
			CEFVersion = bUseExperimentalVersion ? "147.1.5+g68529a5+chromium-147.0.7727.49" : "128.4.13+ge76af7e+chromium-128.0.6613.138";
			CEFPlatform = "linux64";
		}

		if (CEFPlatform.Length > 0 && CEFVersion.Length > 0 && Target.bCompileCEF3)
		{
			string PlatformPath = Path.Combine(Target.UEThirdPartySourceDirectory, "CEF3", "cef_binary_" + CEFVersion + "_" + CEFPlatform);

			PublicSystemIncludePaths.Add(PlatformPath);

			string LibraryPath = Path.Combine(PlatformPath, "Release");
			string RuntimePath = Path.Combine(Target.UEThirdPartyBinariesDirectory, "CEF3", RuntimePlatform, CEFVersion);

			PublicDefinitions.Add(System.String.Format("CEF3_USE_EXPERIMENTAL_VERSION={0}", bUseExperimentalVersion ? "1" : "0"));

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libcef.lib"));

				// There are different versions of the C++ wrapper lib depending on the version of VS we're using
				string VSVersionFolderName = "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName();
				if (Target.WindowsPlatform.Compiler.IsClang())
				{
					VSVersionFolderName += "_ClangCL";
				}
				string WrapperLibraryPath = Path.Combine(PlatformPath, VSVersionFolderName, "libcef_dll_wrapper");

				if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
				{
					WrapperLibraryPath += "/Debug";
				}
				else
				{
					WrapperLibraryPath += "/Release";
				}

				PublicAdditionalLibraries.Add(Path.Combine(WrapperLibraryPath, "libcef_dll_wrapper.lib"));

				List<string> Dlls = new List<string>();
				Dlls.Add("chrome_elf.dll");
				Dlls.Add("d3dcompiler_47.dll");
				Dlls.Add("libcef.dll");
				Dlls.Add("libEGL.dll");
				Dlls.Add("libGLESv2.dll");
				Dlls.Add("dxcompiler.dll");
				Dlls.Add("dxil.dll");
				Dlls.Add("vk_swiftshader.dll");
				Dlls.Add("vulkan-1.dll");

				PublicDelayLoadDLLs.AddRange(Dlls);

				// Add the runtime dlls to the build receipt
				foreach (string Dll in Dlls)
				{
					RuntimeDependencies.Add(Path.Combine(RuntimePath, Dll));
				}

				// We also need the icu translations table required by CEF
				RuntimeDependencies.Add(Path.Combine(RuntimePath, "icudtl.dat"));
				
				// The .pak files are required next to the binaries
				RuntimeDependencies.Add(Path.Combine(RuntimePath, "chrome_100_percent.pak"));
				RuntimeDependencies.Add(Path.Combine(RuntimePath, "chrome_200_percent.pak"));
				RuntimeDependencies.Add(Path.Combine(RuntimePath, "resources.pak"));

				// Add the V8 binary data files as well
				RuntimeDependencies.Add(Path.Combine(RuntimePath, "v8_context_snapshot.bin"));
				if (!bUseExperimentalVersion)
				{
					RuntimeDependencies.Add(Path.Combine(RuntimePath, "snapshot_blob.bin"));
				}

				// And the entire Resources folder. Enumerate the entire directory instead of mentioning each file manually here.
				foreach (string FilePath in Directory.EnumerateFiles(Path.Combine(RuntimePath, "Resources"), "*", SearchOption.AllDirectories))
				{
					RuntimeDependencies.Add(FilePath);
				}
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libcef_dll_wrapper.a"));

				DirectoryReference FrameworkLocation = DirectoryReference.Combine(new DirectoryReference(RuntimePath), "Chromium Embedded Framework.framework");
				// point to the framework in the Binaries/ThirdParty, _outside_ of the .app, for the editor (and for apple on windows mode, just to
				// maintain compatibility)
				if (Target.LinkType == TargetLinkType.Modular || AppleExports.CreatingAppOnWindows(Target.ProjectFile))
				{
					// Add contents of framework directory as runtime dependencies
					foreach (string FilePath in Directory.EnumerateFiles(FrameworkLocation.FullName, "*", SearchOption.AllDirectories))
					{
						RuntimeDependencies.Add(FilePath);
					}
				}
				// for modern 
				else
				{
					FileReference ZipFile = new FileReference(FrameworkLocation.FullName + ".zip");
					// this is relative to module dir
					string FrameworkPath = ZipFile.MakeRelativeTo(new DirectoryReference(ModuleDirectory));

					PublicAdditionalFrameworks.Add(
						new Framework("Chromium Embedded Framework", FrameworkPath, Framework.FrameworkMode.Copy, null)
						);
				}

			}
			else if (Target.Platform == UnrealTargetPlatform.Linux)
			{
				// link against runtime library since this produces correct RPATH
				PublicAdditionalLibraries.Add(Path.Combine(RuntimePath, "libcef.so"));
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libcef_dll_wrapper.a"));

				PrivateRuntimeLibraryPaths.Add(RuntimePath);

				RuntimeDependencies.Add(Path.Combine(RuntimePath, "libcef.so"));
				RuntimeDependencies.Add(Path.Combine(RuntimePath, "libEGL.so"));
				RuntimeDependencies.Add(Path.Combine(RuntimePath, "libGLESv2.so"));
				RuntimeDependencies.Add(Path.Combine(RuntimePath, "chrome-sandbox"));
				RuntimeDependencies.Add(Path.Combine(RuntimePath, "libvk_swiftshader.so"));
				// Skip the older loader included in this package, it will conflict with the engine's
				//RuntimeDependencies.Add(Path.Combine(RuntimePath, "libvulkan.so.1"));

				// We also need the icu translations table required by CEF
				RuntimeDependencies.Add(Path.Combine(RuntimePath, "icudtl.dat"));

				// The .pak files are required next to the binaries
				RuntimeDependencies.Add(Path.Combine(RuntimePath, "chrome_100_percent.pak"));
				RuntimeDependencies.Add(Path.Combine(RuntimePath, "chrome_200_percent.pak"));
				RuntimeDependencies.Add(Path.Combine(RuntimePath, "resources.pak"));
				RuntimeDependencies.Add(Path.Combine(RuntimePath, "vk_swiftshader_icd.json"));

				// Add the V8 binary data files as well
				RuntimeDependencies.Add(Path.Combine(RuntimePath, "v8_context_snapshot.bin"));
				if (!bUseExperimentalVersion)
				{
					RuntimeDependencies.Add(Path.Combine(RuntimePath, "snapshot_blob.bin"));
				}

				// Add the Resources and Swiftshader folders, enumerating the directory contents programmatically rather than listing each file manually here
				List<string> AdditionalDirs = new List<string>();
				AdditionalDirs.Add("Resources");
				
				foreach (string DirName in AdditionalDirs)
				{
					foreach (string FilePath in Directory.EnumerateFiles(Path.Combine(RuntimePath, DirName), "*", SearchOption.AllDirectories))
					{
						RuntimeDependencies.Add(FilePath);
					}
				}
			}
		}
	}
}
