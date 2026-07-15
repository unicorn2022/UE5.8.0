// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DirectoryWatcher : ModuleRules
{
	public DirectoryWatcher(ReadOnlyTargetRules Target) : base(Target)
	{
		bRequiresPlatformSDK = true;

		PrivateDependencyModuleNames.Add("Core");
		CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Error;
	}
}
