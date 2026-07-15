// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class MetalCPP : ModuleRules
{
	public MetalCPP(ReadOnlyTargetRules Target) : base(Target)
	{
		bRequiresPlatformSDK = true;
		
		string MTLCPPPath = Target.UEThirdPartySourceDirectory + "MetalCPP/";

		if (Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.IOS ||
            Target.Platform == UnrealTargetPlatform.TVOS || Target.Platform == UnrealTargetPlatform.VisionOS)
		{
			Type = ModuleType.CPlusPlus;
			
			if(Target.Platform == UnrealTargetPlatform.Mac)
			{
				PublicFrameworks.Add("QuartzCore");
			}	
			PublicWeakFrameworks.Add("Metal");
			PublicSystemIncludePaths.Add(MTLCPPPath);
		}
    }
}
