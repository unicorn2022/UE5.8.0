// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class IREE : ModuleRules
{
	public IREE(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "Include"));

		bool bUseLibraryDeveloperVersion =
			Target.Configuration == UnrealTargetConfiguration.Debug ||
			Target.Configuration == UnrealTargetConfiguration.DebugGame ||
			Target.Configuration == UnrealTargetConfiguration.Development;

		if(bUseLibraryDeveloperVersion)
		{
			PrivateDependencyModuleNames.Add("IREETracing");
		}

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string LibraryName = bUseLibraryDeveloperVersion ? "ireert_dev.lib" : "ireert.lib";

			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "Lib", "Windows", "flatcc_parsing.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "Lib", "Windows", LibraryName));

			if (Target.WindowsPlatform.Compiler.IsClang())
			{
				PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "Include", "Clang"));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			string LibraryName = bUseLibraryDeveloperVersion ? "libireert_dev.a" : "libireert.a";
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "Lib", "Linux", "libflatcc_parsing.a"));
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "Lib", "Linux", LibraryName));
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string LibraryName = bUseLibraryDeveloperVersion ? "libireert_dev.a" : "libireert.a";
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "Lib", "Mac", "libflatcc_parsing.a"));
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "Lib", "Mac", LibraryName));
		}

		if (Target.Type == TargetType.Editor)
		{
			string BinariesPath = Path.Combine(PluginDirectory, "Binaries", "ThirdParty", "IREE");

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Windows", "iree-compile.exe"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Windows", "IREECompiler.dll"));

				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Windows", "lld-link.exe"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Windows", "clang_rt.builtins-x86_64.lib"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Windows", "libfltused.lib"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Windows", "ucrtbase_applocal.lib"));

				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Windows", "torch-mlir-import-onnx.exe"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Windows", "abseil_dll.dll"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Windows", "libprotobuf.dll"));
			}
			else if (Target.Platform == UnrealTargetPlatform.Linux)
			{
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Linux", "iree-compile"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Linux", "ld.lld"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Linux", "libIREECompiler.so"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Linux", "torch-mlir-import-onnx"));
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Mac", "iree-compile"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Mac", "ld.lld"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Mac", "ld64.lld"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Mac", "libIREECompiler.dylib"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Mac", "torch-mlir-import-onnx"));
			}
		}

		PublicDefinitions.Add("IREE_SYNCHRONIZATION_DISABLE_UNSAFE=1");
	}
}
