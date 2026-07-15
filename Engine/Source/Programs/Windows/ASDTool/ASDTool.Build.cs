// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ASDTool : ModuleRules
{
	public ASDTool(ReadOnlyTargetRules Target) : base(Target)
	{
		bTreatAsEngineModule = true;

		PrivateIncludePathModuleNames.AddRange(new string[] {
			"Launch",
			"TargetPlatform"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"ApplicationCore",
			"Projects",
			"ShaderCompilerCommon",
			"RenderCore",
			"RHI",
			"RHICore",
			"PakFile",
			"PakFileUtilities",
			"SQLiteCore"
		});

		bEnableExceptions = true;

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Microsoft))
		{
			PrivateDependencyModuleNames.Add("D3D12RHI");
		}

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "AgilitySDK");
			PublicSystemLibraries.AddRange(new string[] { "d3d12.lib", "dxgi.lib" });

			// Expose the AgilitySDK version string as a preprocessor define so runtime code
			// can locate compiler binaries without hardcoding the version.
			PrivateDefinitions.Add($"AGILITY_SDK_VERSION_STRING=\"{AgilitySDK.DefaultVersion}\"");
		}
	}
}
