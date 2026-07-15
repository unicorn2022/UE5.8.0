// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MeshDescription : ModuleRules
	{
		public MeshDescription(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject"
				}
			);

			if (Target.bBuildWithEditorOnlyData && Target.Platform.IsInGroup(UnrealPlatformGroup.Desktop))
			{
				PrivateDependencyModuleNames.Add("DerivedDataCache");
			}
		}
	}
}
