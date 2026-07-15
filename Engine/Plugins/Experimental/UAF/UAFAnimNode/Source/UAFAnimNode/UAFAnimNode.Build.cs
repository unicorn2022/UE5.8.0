// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UAFAnimNode : ModuleRules
	{
		public UAFAnimNode(ReadOnlyTargetRules Target) : base(Target)
		{
			bAllowUETypesInNamespaces = true;

			PublicDependencyModuleNames.AddRange(new string[] { "UAF" });
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"RigVM",
					"ControlRig",
					"Engine",
					"UAFAnimGraph",
					"TraceLog",
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
					}
				);
			}

		}
	}
}
