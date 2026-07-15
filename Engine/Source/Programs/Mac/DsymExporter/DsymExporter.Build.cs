// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DsymExporter : ModuleRules
{
	public DsymExporter( ReadOnlyTargetRules Target ) : base(Target)
	{
		PrivateIncludePathModuleNames.Add( "Launch" );
		bRequiresPlatformSDK = true;
	
		PrivateDependencyModuleNames.AddRange(
			new string[] 
			{ 
				"Core",
				"ApplicationCore",
				"Projects"
			}
			);
	}
}
