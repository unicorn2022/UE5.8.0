// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using UnrealBuildTool.Rules;

public class MetaHumanLocalLiveLinkSource : ModuleRules
{
	public MetaHumanLocalLiveLinkSource(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] {
			"MetaHumanLiveLinkSource",
		});
			
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"LiveLink",
			"LiveLinkInterface",
			"MediaUtils",
			"MediaAssets",
			"MediaFrameworkUtilities",
			"MediaProfile",
			"SlateCore",
			"Slate",
			"InputCore",
			"UMG",
			"Engine",
			"AudioPlatformConfiguration",
			"MetaHumanPipelineCore",
			"MetaHumanImageViewer",
			"MetaHumanCoreTech",
			"SpeechAnimationSolver",
		});

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"AudioMixerWasapi",
			});
		}

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.Add("EditorWidgets");
			PrivateDependencyModuleNames.Add("UnrealEd");
			PrivateDependencyModuleNames.Add("PropertyEditor");
		}
	}
}
