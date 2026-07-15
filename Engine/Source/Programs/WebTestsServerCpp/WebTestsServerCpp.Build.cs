// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class WebTestsServerCpp : ModuleRules
{
	public WebTestsServerCpp(ReadOnlyTargetRules Target) : base(Target)
	{
		bUseUnity = false;

		PublicIncludePathModuleNames.Add("Launch");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"ApplicationCore",
				"Projects",
				"HTTPServer",
				"WebSocketServer",
				"Sockets",
				"Json",
			});
	}
}
