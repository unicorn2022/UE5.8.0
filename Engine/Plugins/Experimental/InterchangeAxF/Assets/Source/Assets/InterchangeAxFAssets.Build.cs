// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class InterchangeAxFAssets : ModuleRules
	{
		public InterchangeAxFAssets(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"Projects",
					"RenderCore"
				}
			);
		}
	}
}
