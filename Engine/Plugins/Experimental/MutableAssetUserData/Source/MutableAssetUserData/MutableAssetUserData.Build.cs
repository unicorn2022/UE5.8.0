// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MutableAssetUserData : ModuleRules
	{
        public MutableAssetUserData(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add(ModuleDirectory + "/Private");
			PublicIncludePaths.Add(ModuleDirectory + "/Public");
			
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Engine",
					"Core",
					"CoreUObject",
					"MutableRuntime"
				});

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CustomizableObject"
				});
		}
	}
}
