// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class libcurl : ModuleRules
{
	private readonly bool bDebugMode = false;
	public libcurl(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicDefinitions.Add("WITH_LIBCURL=1");
		PublicDefinitions.Add("CURL_STATICLIB=1");
		if (Target.Configuration == UnrealTargetConfiguration.Test || Target.Configuration == UnrealTargetConfiguration.Shipping)
		{
			bDebugMode = false;
		}
		
		if (bDebugMode)
		{
			PublicDefinitions.Add("LIBCURL_EXTERNAL_ASSERT_HANDLER=1");
		}

		string LibCurlPath = Target.UEThirdPartySourceDirectory + "libcurl/8.15.0/";
		PublicSystemIncludePaths.Add(Path.Combine(LibCurlPath, "include"));

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			if (bDebugMode)
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibCurlPath, "lib", "Unix", Target.Architecture.LinuxName, "Debug", "libcurl-d.a"));	
			}
			else
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibCurlPath, "lib", "Unix", Target.Architecture.LinuxName, "Release", "libcurl.a"));
			}
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			string[] Architectures = new string[] {
				"ARM64",
				"x64",
			};
 
			foreach(var Architecture in Architectures)
			{
				if (bDebugMode)
				{
					PublicAdditionalLibraries.Add(Path.Combine(LibCurlPath, "lib", "Android", Architecture, "Debug", "libcurl-d.a"));
				}
				else
				{
					PublicAdditionalLibraries.Add(Path.Combine(LibCurlPath, "lib", "Android", Architecture, "Release", "libcurl.a"));
				}
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			if (bDebugMode)
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibCurlPath, "lib", "Mac", "Debug", "libcurl-d.a"));
			}
			else
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibCurlPath, "lib", "Mac", "Release", "libcurl.a"));
			}

			PublicFrameworks.Add("SystemConfiguration");
		}
		else if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) && !Target.WindowsPlatform.bUseXCurl)
		{
			string PlatformSubdir = "Win64";
			if (Target.Architecture == UnrealArch.Arm64)
			{
				// BuildForUE puts the arm64 in <Platform>/<Arch>
				PlatformSubdir = Path.Combine(PlatformSubdir, "arm64");
			}

			if (bDebugMode)
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibCurlPath, "lib", PlatformSubdir, "Debug", "libcurl-d.lib"));
			}
			else
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibCurlPath, "lib", PlatformSubdir, "Release", "libcurl.lib"));
			}
		}

		// Our build requires nghttp2, OpenSSL and zlib, so ensure they're linked in
		AddEngineThirdPartyPrivateStaticDependencies(Target, new string[]
		{
			"nghttp2",
			"OpenSSL",
			"zlib"
		});
	}
}
