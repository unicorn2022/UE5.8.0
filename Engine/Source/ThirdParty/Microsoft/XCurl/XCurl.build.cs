// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class XCurl : ModuleRules
{
	protected virtual bool bPlatformUsesXCurlLibraries => (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) && Target.WindowsPlatform.bUseXCurl);

	public XCurl(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		bool bHasXCurl = false;

		// Add XCurl if required
		if (bPlatformUsesXCurlLibraries && GRDK.IsValid(Target))
		{		
			if (GRDK.IsLegacyFolderStructure())
			{
				GRDK.AddLegacyExtensionDependency(Target, this, "Xbox.XCurl.API", "XCurl");
			}
			else
			{
				GRDK.AddDependency(Target, this, "XCurl");
			}


			PublicDefinitions.Add("PLATFORM_CURL_INCLUDE=\"XCurl.h\"");

			// GDK documentation says that XCurl is equivalent to libCurl compiled with this flag
			// (and other flags, but this is the only one that is still checked in XCurl.h)
			PublicDefinitions.Add("CURL_NO_OLDIES"); // causes XCurl.h to define CURL_STRICTER which is needed to get the proper declaration for CURL*								
				
			bHasXCurl = true;

		}

		PublicDefinitions.Add("WITH_XCURL=" + (bHasXCurl ? "1" : "0"));
	}
}

