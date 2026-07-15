// Copyright (C) 2022 Apple Inc. All Rights Reserved.
using UnrealBuildTool;
using EpicGames.Core;
using System;
using System.IO;

public class MetalShaderConverter : ModuleRules
{
	/// <summary>
	/// Checks if bindless is enabled for any supported iOS shader platform.
	/// Returns true if framework should be included for iOS.
	/// </summary>
	private static bool ShouldIncludeMetalShaderConverterForIOS(ReadOnlyTargetRules Target)
	{
		// Read Engine config hierarchy for iOS platform
		ConfigHierarchy Ini = ConfigCache.ReadHierarchy(
			ConfigHierarchyType.Engine,
			DirectoryReference.FromFile(Target.ProjectFile),
			UnrealTargetPlatform.IOS);

		// Read platform support settings from IOSRuntimeSettings
		Ini.GetBool("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bSupportsMetal", out bool bSupportsMetal);
		Ini.GetBool("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bSupportsMetalMobileSM5", out bool bSupportsMetalMobileSM5);
		Ini.GetBool("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bSupportsMetalMobileSM6", out bool bSupportsMetalMobileSM6);

		// Check each shader platform: bindless must be enabled AND platform must be supported
		// METAL_ES3_1_IOS
		if (bSupportsMetal)
		{
			if (Ini.TryGetValue("ShaderPlatformConfig METAL_ES3_1_IOS", "BindlessConfiguration", out string ES31Config) &&
				!string.IsNullOrEmpty(ES31Config) && !ES31Config.Equals("Disabled", StringComparison.OrdinalIgnoreCase))
			{
				return true;
			}
		}

		// METAL_SM5_IOS
		if (bSupportsMetalMobileSM5)
		{
			if (Ini.TryGetValue("ShaderPlatformConfig METAL_SM5_IOS", "BindlessConfiguration", out string SM5Config) &&
				!string.IsNullOrEmpty(SM5Config) && !SM5Config.Equals("Disabled", StringComparison.OrdinalIgnoreCase))
			{
				return true;
			}
		}

		// METAL_SM6_IOS
		if (bSupportsMetalMobileSM6)
		{
			if (Ini.TryGetValue("ShaderPlatformConfig METAL_SM6_IOS", "BindlessConfiguration", out string SM6Config) &&
				!string.IsNullOrEmpty(SM6Config) && !SM6Config.Equals("Disabled", StringComparison.OrdinalIgnoreCase))
			{
				return true;
			}
		}

		return false;
	}

	public MetalShaderConverter(ReadOnlyTargetRules Target) : base(Target)
	{
		bRequiresPlatformSDK = true;
		
		string SourcePath = Path.Combine(Target.UEThirdPartySourceDirectory,"Apple", "MetalShaderConverter");
		string IncludePath = Path.Combine(SourcePath, "include");
		string BinariesPath = Path.Combine(Target.UEThirdPartyBinariesDirectory, "Apple", "MetalShaderConverter");

		Type = ModuleType.External;
		PCHUsage = PCHUsageMode.NoPCHs;
			
		PublicDependencyModuleNames.Add("Core");
			
		if (Target.Platform == UnrealTargetPlatform.Mac ||
			Target.Platform == UnrealTargetPlatform.IOS ||
			Target.Platform == UnrealTargetPlatform.TVOS ||
			Target.Platform == UnrealTargetPlatform.VisionOS)
		{
			Type = ModuleType.CPlusPlus;

			PublicIncludePaths.Add(Path.Combine(IncludePath, "metal_irconverter"));
			PublicIncludePaths.Add(Path.Combine(IncludePath, "metal_irconverter_runtime"));
			
			PublicDependencyModuleNames.Add("MetalCPP");
						
			PublicWeakFrameworks.Add("Metal");
			
			PublicIncludePaths.Add(Path.Combine(IncludePath, "metal_irconverter_ext"));
			PublicIncludePaths.Add(Path.Combine(IncludePath, "mac"));		
			
			PublicFrameworks.Add("QuartzCore");
		
			if(Target.Platform == UnrealTargetPlatform.Mac)
			{
				string DylibPath = Path.Combine(BinariesPath, "Mac", "libmetalirconverter.dylib");
				PublicAdditionalLibraries.Add(DylibPath);
				RuntimeDependencies.Add(DylibPath);
				
				PublicDefinitions.Add("METAL_USE_METAL_SHADER_CONVERTER=1");
			}
			else if (Target.Platform == UnrealTargetPlatform.IOS)
			{
				// Only include framework if bindless is enabled for any supported iOS shader platform
				if (ShouldIncludeMetalShaderConverterForIOS(Target))
				{
					string FrameworkPath = Path.Combine(ModuleDirectory, "../../../../Binaries/ThirdParty/Apple/MetalShaderConverter/IOS/libmetalirconverter.embeddedframework.zip");

					// Embed the pre-built libmetalirconverter framework into the app bundle
					PublicAdditionalFrameworks.Add(new Framework(
							"libmetalirconverter",
							FrameworkPath,
							Framework.FrameworkMode.Copy));

					// Weak-link so the app launches even if the framework is unavailable at runtime
					PublicWeakFrameworks.Add("libmetalirconverter");
					
					PublicDefinitions.Add("METAL_USE_METAL_SHADER_CONVERTER=1");
				}
			}
			// TODO: tvOS and VisionOS
		}
		
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicIncludePaths.Add(Path.Combine(IncludePath, "metal_irconverter"));
			PublicIncludePaths.Add(Path.Combine(IncludePath, "metal_irconverter_runtime"));
			PublicIncludePaths.Add(Path.Combine(IncludePath, "metal_irconverter_ext"));
			PublicAdditionalLibraries.Add(Path.Combine(SourcePath, "lib", "metalirconverter.lib"));

			string DynamicLibName = "metalirconverter.dll";
			RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", DynamicLibName), Path.Combine(BinariesPath, "Windows", DynamicLibName));
			
			PublicDefinitions.Add("METAL_USE_METAL_SHADER_CONVERTER=1");
		}
	}
}
