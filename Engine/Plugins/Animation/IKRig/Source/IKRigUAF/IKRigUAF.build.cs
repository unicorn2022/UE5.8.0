// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class IKRigUAF : ModuleRules
	{
		public IKRigUAF(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					
					"RigVM",
					"UAF",
					"UAFAnimGraph",
					
					"IKRig"
				}
			);
		}
	}
}