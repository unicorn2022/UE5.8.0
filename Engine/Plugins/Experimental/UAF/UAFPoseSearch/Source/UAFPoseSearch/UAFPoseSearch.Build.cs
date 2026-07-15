// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UAFPoseSearch : ModuleRules
	{
		public UAFPoseSearch(ReadOnlyTargetRules Target) : base(Target)
		{
			bAllowUETypesInNamespaces = true;

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Chooser",
					"Core",
					"CoreUObject",
					"Engine",
					"EvaluationNotifiesRuntime",
					"HierarchyTableAnimationRuntime",
					"HierarchyTableRuntime",
					"PoseSearch",
					"RigVM",
					"UAF",
					"UAFAnimGraph",
					"UAFAnimNode"
				}
			);
		}
	}
}