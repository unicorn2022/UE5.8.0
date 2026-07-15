// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedPlatforms(UnrealPlatformClass.Desktop)]
public class WebTestsServerCppTarget : TargetRules
{
	public WebTestsServerCppTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Program;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		LinkType = TargetLinkType.Monolithic;
		LaunchModuleName = "WebTestsServerCpp";

		bBuildDeveloperTools = false;
		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = false;
		bCompileAgainstApplicationCore = true;
		bCompileICU = false;
		bIsBuildingConsoleApplication = true;
	}
}
