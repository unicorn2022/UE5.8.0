// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class Scribble : ModuleRules
    {
        public Scribble(ReadOnlyTargetRules Target) : base(Target)
        {
			NumIncludedBytesPerUnityCPPOverride = 688128; // best unity size found from using UBT ProfileUnitySizes mode

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core",
                    "CoreUObject",
                    "SlateCore",
                    "Slate",
                    "Engine",
				}
            );

            PublicDependencyModuleNames.AddRange(
                new string[]
                {
	                "DeveloperSettings",
                }
            );
        }
    }
}
