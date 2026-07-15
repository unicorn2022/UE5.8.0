// Copyright Epic Games, Inc. All Rights Reserved.
namespace UnrealBuildTool.Rules
{
	public class FastGeoStreamingEditor : ModuleRules
	{
		public FastGeoStreamingEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AssetDefinition",
					"AssetTools",
					"Core",
					"CoreUObject",
					"Engine",
					"FastGeoStreaming",
					"Slate",
					"SlateCore",
					"UnrealEd"
				}
			);
		}
	}
}