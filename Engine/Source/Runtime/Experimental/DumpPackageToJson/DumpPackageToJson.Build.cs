// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DumpPackageToJson : ModuleRules
{
	public DumpPackageToJson(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.AddRange(new string[] {
			"StorageServerClient",
		});

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"JsonObjectGraph",
				"StorageServerClient",
			}
		);

		PCHUsage = PCHUsageMode.NoPCHs;
		bEnableExceptions = true;
	}
}
