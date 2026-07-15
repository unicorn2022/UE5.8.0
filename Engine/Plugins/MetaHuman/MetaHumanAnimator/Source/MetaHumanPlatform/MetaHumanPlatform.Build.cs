// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanPlatform : ModuleRules
{
	public MetaHumanPlatform(ReadOnlyTargetRules Target) : base(Target)
	{
		bRequiresPlatformSDK = true;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"MeshTrackerInterface",
			"MetaHumanCore",
			"RHI",
		});

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"D3D12RHI",
				"DX12"
			});
		}

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Linux))
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"VulkanRHI",
			});
		}

		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"MetalRHI",
			});
		}
	}
}
