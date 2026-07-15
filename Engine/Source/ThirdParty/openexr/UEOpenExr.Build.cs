// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.IO;
using UnrealBuildTool;

public class UEOpenExr : ModuleRules
{
	public UEOpenExr(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicDependencyModuleNames.Add("Imath");

		bool bDebug = (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT);

		string DeploymentDirectory = Path.Combine(ModuleDirectory, "Deploy", "openexr-3.4.3");

		PublicSystemIncludePaths.Add(Path.Combine(DeploymentDirectory, "include"));

		// XXX: OpenEXR includes some of its own headers without the
		// leading "OpenEXR/..."
		PublicSystemIncludePaths.Add(Path.Combine(DeploymentDirectory, "include", "OpenEXR"));

		List<string> OpenEXRLibraries = new List<string> {
			"Iex",
			"IlmThread",
			"OpenEXR",
			"OpenEXRCore",
			"OpenEXRUtil"
		};

		string DebugLibPostfix = bDebug ? "_d" : "";
		string ExrLibPostfix = "-3_4" + DebugLibPostfix;

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
		{
			string LibDirectory = Path.Combine(
				DeploymentDirectory,
				"VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(),
				Target.Architecture.WindowsLibDir,
				"lib");

			foreach (string OpenEXRLibrary in OpenEXRLibraries)
			{
				PublicAdditionalLibraries.Add(
					Path.Combine(LibDirectory, OpenEXRLibrary + ExrLibPostfix + ".lib"));
			}

			PublicAdditionalLibraries.Add(
				Path.Combine(LibDirectory, "openjph.0.24" + DebugLibPostfix + ".lib"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string LibDirectory = Path.Combine(
				DeploymentDirectory,
				"Mac",
				"lib");

			foreach (string OpenEXRLibrary in OpenEXRLibraries)
			{
				PublicAdditionalLibraries.Add(
					Path.Combine(LibDirectory, "lib" + OpenEXRLibrary + ExrLibPostfix + ".a"));
			}

			PublicAdditionalLibraries.Add(
				Path.Combine(LibDirectory, "libopenjph" + DebugLibPostfix + ".a"));
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			string LibDirectory = Path.Combine(
				DeploymentDirectory,
				"Unix",
				Target.Architecture.LinuxName,
				"lib");

			foreach (string OpenEXRLibrary in OpenEXRLibraries)
			{
				PublicAdditionalLibraries.Add(
					Path.Combine(LibDirectory, "lib" + OpenEXRLibrary + ExrLibPostfix + ".a"));
			}

			PublicAdditionalLibraries.Add(
				Path.Combine(LibDirectory, "libopenjph" + DebugLibPostfix + ".a"));
		}
	}
}
