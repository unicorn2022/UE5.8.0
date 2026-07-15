// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class XSAPI : ModuleRules
{
	public XSAPI(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		bool bHasXSAPI = false;
		if (GRDK.IsValid(Target) )
		{
			if (GRDK.IsLegacyFolderStructure())
			{
				GRDK.AddLegacyExtensionDependency(Target, this, "Xbox.Services.API.C", "Microsoft.Xbox.Services.142.GDK.C", bWithDLL:false, ExtraLibSubPath:"Release/v142");	
				GRDK.AddLegacyExtensionDependency(Target, this, "Xbox.LibHttpClient", "libHttpClient.GDK");
				GRDK.AddLegacyExtensionDependency(Target, this, "Xbox.XCurl.API", "XCurl", bWithLib:false); // only libHttpClient depends on XCurl.dll so we don't need the lib
			}
			else
			{
				int VCToolset = (GRDK.GetGDKEdition() >= 260400) ? 143 : 142;
				GRDK.AddDependency(Target, this, $"Microsoft.Xbox.Services.{VCToolset}.C", bWithDLL:false );
				GRDK.AddDependency(Target, this, "libHttpClient");
				GRDK.AddDependency(Target, this, "XCurl", bWithLib:false); // only libHttpClient depends on XCurl.dll so we don't need the lib

			}

			bHasXSAPI = true;
		}

		PublicSystemLibraries.Add("appnotify.lib");

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			PublicDefinitions.Add("_GAMING_DESKTOP");
		}

		// libHttpClient depends on WinHttp
		PrivateDependencyModuleNames.Add("WinHttp");

		PublicDefinitions.Add("WITH_XSAPI_C=" + (bHasXSAPI ? "1" :"0"));
	}
}

