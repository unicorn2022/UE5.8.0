// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class LiveLinkFbxProvider : ModuleRules
{
	public LiveLinkFbxProvider(ReadOnlyTargetRules Target) : base(Target)
	{
		IWYUSupport = IWYUSupport.None;
		bUseRTTI = false;

		// Unreal dependency modules
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ApplicationCore",
			"Core",
			"CoreUObject",
			"Projects",
			"LiveLinkInterface",
			"LiveLinkMessageBusFramework",
			"Messaging",
			"UdpMessaging",
		});

		PrivateIncludePathModuleNames.AddRange(new string[] {
			"Launch",
		});

		AddEngineThirdPartyPrivateStaticDependencies(Target, "FBX");
	}
}
