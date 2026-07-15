// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedPlatforms("IOS", "TVOS", "VisionOS")]
public class BackgroundHttpIOS : ModuleRules
{
    public BackgroundHttpIOS(ReadOnlyTargetRules Target) : base(Target)
    {
		bRequiresPlatformSDK = true;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
				"BackgroundHTTPFileHash"
            }
        );

		PublicIncludePathModuleNames.AddRange(
            new string[] {
                "BackgroundHTTP",
            }
        );

		PrivateDependencyModuleNames.Add("BuildSettings");
    }
}
