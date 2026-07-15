// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UAFChooserUncookedOnly : ModuleRules
	{
		public UAFChooserUncookedOnly(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CoreUObject",
					"Engine",
					"ChooserEditor",
					"UAF", 
					"UAFEditor", 
					"UAFUncookedOnly",
					"UAFAnimGraph",
					"UAFAnimGraphUncookedOnly",
					"WorkspaceEditor"
				}
			);
		}
	}
}