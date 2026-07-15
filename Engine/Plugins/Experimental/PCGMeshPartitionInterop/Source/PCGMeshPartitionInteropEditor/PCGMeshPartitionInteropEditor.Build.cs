// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class PCGMeshPartitionInteropEditor: ModuleRules
	{
		public PCGMeshPartitionInteropEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
			bAllowUETypesInNamespaces = true;

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"EditorSubsystem",
					"Engine",
					"PCG",
					"MeshPartition",
					"MeshPartitionEditor",
					"GeometryCore",
					"GeometryFramework",
				}
			);
			
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"MeshConversion",
					"MeshConversionEngineTypes",
					"PCGMeshPartitionInterop",
					"PCGGeometryScriptInterop",
					"UnrealEd", // GEditor
				}
			);
		}
	}
}