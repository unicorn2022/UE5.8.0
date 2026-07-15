// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ProceduralVegetation : ModuleRules
	{
		public ProceduralVegetation(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"DynamicWind"
				});

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Chaos",
					"ChaosCore",
					"Core",
					"CoreUObject",
					"Engine",
					"GeometryCore",
					"GeometryFramework",
					"Json",
					"JsonUtilities",
					"MeshDescription",
					"MeshConversionEngineTypes",
					"ModelingComponents",
					"PCG",
					"RenderCore",
					"StaticMeshDescription",
					"RHI",
					"Projects",
					"ImageCore"
				}
			);
		}
	}
}