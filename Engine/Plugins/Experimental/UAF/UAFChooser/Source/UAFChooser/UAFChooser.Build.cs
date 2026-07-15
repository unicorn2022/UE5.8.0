// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UAFChooser : ModuleRules
	{
		public UAFChooser(ReadOnlyTargetRules Target) : base(Target)
		{
			bAllowUETypesInNamespaces = true;
			
			PublicDependencyModuleNames.AddRange(new string[] { "UAF", "Chooser"});
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"RigVM",
					"ControlRig",
					"Engine",
					"Chooser",
					"UAFAnimGraph",
					"UAFAnimNode",
				}
			);
			
			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"UAFUncookedOnly",
						"UAFEditor",
						"WorkspaceEditor",
						"UAFChooserUncookedOnly"
					}
				);
			}
			
		}
	}
}