// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UAFAnimNodeUncookedOnly : ModuleRules
	{
		public UAFAnimNodeUncookedOnly(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"UAFAnimNode",
				}
			);
		}
	}
}
