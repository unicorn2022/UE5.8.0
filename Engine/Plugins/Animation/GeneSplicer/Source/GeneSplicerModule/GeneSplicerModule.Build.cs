// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class GeneSplicerModule : ModuleRules
	{
		public GeneSplicerModule(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"RigLogicModule",
					"Projects"
				} 
			);

			if (Target.Type == TargetType.Editor)
			{
				PublicDependencyModuleNames.Add("UnrealEd");
				PublicDependencyModuleNames.Add("MessageLog");
			}

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"RigLogicModule",
					"RigLogicLib",
					"GeneSplicerLib"
				}
			 );
		}
	}
}
