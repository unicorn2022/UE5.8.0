// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class GeometryCollectionNodes : ModuleRules
	{
        public GeometryCollectionNodes(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"ChaosCore",
					"Chaos",
					"DataflowCore",
					"DataflowEngine",
					"DataflowEnginePlugin",
					"DataflowNodes",
					"GeometryCollectionEngine",
					"GeometryCore",
					"GeometryDataflowNodes",
					"Voronoi",
					"GeometryFramework",
					"MeshDescription",
					"StaticMeshDescription",
					"PlanarCut",
					"Engine",
					"DataflowMedialSkeleton"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[] 
				{
					"DynamicMesh",
					"MeshConversion",
					"MeshConversionEngineTypes",
					"FractureEngine",
					"GeometryAlgorithms",
					"SkeletalMeshDescription"
				}
			);

			if (Target.bCompileAgainstEditor)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"DataflowEditor",
						"UnrealEd",
						"SkeletalMeshUtilitiesCommon", // for skeletal mesh conversion
						"ModelingComponentsEditorOnly", // for FSubdividePoly (OpenSubdiv)
					}
					);
			}
		}
	}
}
