// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

[SupportedPlatformGroups("Windows")]
public class Detours : ModuleRules
{
	public Detours(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		// ARM64EC uses x64 library via emulation
		UnrealArch DetoursArch = Target.Architecture;
		if (DetoursArch == UnrealArch.Arm64ec)
		{
			DetoursArch = UnrealArch.X64;
		}

		string VcRootDir = $"{DetoursArch}-windows-static{(Target.bUseStaticCRT ? string.Empty : "-md")}";
		string PkgDir = $"detours_{DetoursArch}-windows-static{(Target.bUseStaticCRT ? string.Empty : "-md")}";

		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "Windows", VcRootDir, PkgDir, "include"));

		PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "Windows", VcRootDir, PkgDir, "lib", "detours.lib"));
	}
}
