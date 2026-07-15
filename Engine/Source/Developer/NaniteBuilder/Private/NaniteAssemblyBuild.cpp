// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteAssemblyBuild.h"
#include "NaniteIntermediateResources.h"
#include "NaniteBuilder.h"
#include "Rendering/NaniteResources.h"
#include "ClusterDAG.h"
#include "Cluster.h"
#include "Encode/NaniteEncodeSkinning.h"

namespace Nanite
{

static void AddAssemblyPart(
	FIntermediateResources& AssemblyResources,
	const FIntermediateResources& PartResources,
	const FMaterialRemapTable& MaterialRemap,
	uint32 AssemblyPartIndex)
{
	FClusterDAG& DstDAG = AssemblyResources.ClusterDAG;
	const FClusterDAG& SrcDAG = PartResources.ClusterDAG;

	check( AssemblyPartIndex != MAX_uint32 );

	const FAssemblyPartData& PartData = DstDAG.AssemblyPartData[ AssemblyPartIndex ];
	check( PartData.NumInstances > 0 );

	// Combine the part's contribution to the final product, multiplied by number of instances where applicable
	AssemblyResources.NumInputTriangles += PartResources.NumInputTriangles * PartData.NumInstances;
	AssemblyResources.NumInputVertices  += PartResources.NumInputVertices  * PartData.NumInstances;

	DstDAG.bHasSkinning	|= SrcDAG.bHasSkinning;
	DstDAG.bHasTangents	|= SrcDAG.bHasTangents;
	DstDAG.bHasColors	|= SrcDAG.bHasColors;

	DstDAG.MaxTexCoords = FMath::Max(DstDAG.MaxTexCoords, SrcDAG.MaxTexCoords);

	// Add the part's transformed bounds into the final product as well
	for( uint32 i = 0; i < PartData.NumInstances; i++ )
	{
		const FMatrix44f& Transform = DstDAG.AssemblyInstanceData[ PartData.FirstInstance + i ].Transform;
		const float MaxScale = Transform.GetScaleVector().GetMax();
		DstDAG.TotalBounds += SrcDAG.TotalBounds.TransformBy( Transform );
		DstDAG.SurfaceArea += SrcDAG.SurfaceArea * FMath::Square( MaxScale );
	}

#if RAY_TRACE_VOXELS
	RTCScene EmbreeScene = nullptr;
	if( DstDAG.Settings.ShapePreservation == ENaniteShapePreservation::Voxelize )
	{
		EmbreeScene = rtcNewScene( DstDAG.RayTracingScene.Device );
	}
#endif

	TArray<uint32> GroupRemap;
	GroupRemap.Init(MAX_uint32, SrcDAG.Groups.Num());

	// Copy groups and clusters
	const int32 FirstCopiedCluster = DstDAG.Clusters.Num();	
	const int32 NumGroupsToCopy = SrcDAG.Groups.Num();
	for (int32 SrcGroupIndex = 0; SrcGroupIndex < NumGroupsToCopy; ++SrcGroupIndex)
	{
		const FClusterGroup& SrcGroup = SrcDAG.Groups[SrcGroupIndex];

		if( SrcGroup.bTrimmed || SrcGroup.MeshIndex != 0 )
			continue;

		// Copy group
		const uint32 DstGroupIndex = DstDAG.Groups.Num();
		FClusterGroup& DstGroup = DstDAG.Groups.Add_GetRef(SrcGroup);
		GroupRemap[SrcGroupIndex] = DstGroupIndex;

		check(DstGroup.AssemblyPartIndex == MAX_uint32);
		DstGroup.AssemblyPartIndex = AssemblyPartIndex;

		DstGroup.Children.Reset();
		for( FClusterRef SrcClusterRef : SrcGroup.Children )
		{
			// Copy cluster
			const uint32 SrcClusterIndex = SrcClusterRef.ClusterIndex;
			const uint32 DstClusterIndex = DstDAG.Clusters.Add( SrcDAG.Clusters[ SrcClusterIndex ] );

			DstDAG.Clusters[ DstClusterIndex ].GroupIndex = DstGroupIndex;
			DstDAG.Groups[ DstGroupIndex ].Children.Add( FClusterRef( DstClusterIndex ) );

		#if RAY_TRACE_VOXELS
			// TODO Doesn't work with trim.
			if( EmbreeScene && DstDAG.Clusters[ DstClusterIndex ].MipLevel == 0 )
				FRayTracingScene::AddCluster( DstDAG.RayTracingScene.Device, EmbreeScene, DstDAG.Clusters[ DstClusterIndex ] );
		#endif
		}
	}

	check( FirstCopiedCluster < DstDAG.Clusters.Num() );

#if RAY_TRACE_VOXELS
	if( EmbreeScene )
	{
		rtcCommitScene( EmbreeScene );

		for( uint32 i = 0; i < PartData.NumInstances; i++ )
		{
			uint32 InstanceIndex = PartData.FirstInstance + i;
			DstDAG.RayTracingScene.AddInstance( EmbreeScene, DstDAG.AssemblyInstanceData[ InstanceIndex ].Transform, InstanceIndex, FirstCopiedCluster );
		}

		rtcReleaseScene( EmbreeScene );
	}
#endif

	// Run through and fix up the copied clusters
	for (int32 ClusterIndex = FirstCopiedCluster; ClusterIndex < DstDAG.Clusters.Num(); ++ClusterIndex)
	{
		FCluster& Cluster = DstDAG.Clusters[ClusterIndex];
		
		// Remap groups
		if( Cluster.GeneratingGroupIndex != MAX_uint32 )
			Cluster.GeneratingGroupIndex = GroupRemap[ Cluster.GeneratingGroupIndex ];

		// Remap materials
		for( int32& MaterialIndex : Cluster.MaterialIndexes )
			MaterialIndex = MaterialRemap[ MaterialIndex ];

		for( FMaterialRange& MaterialRange : Cluster.MaterialRanges )
			MaterialRange.MaterialIndex = MaterialRemap[ MaterialRange.MaterialIndex ];
	}

	const FClusterGroup& RootGroup = DstDAG.Groups.Last();

	// TODO support more than 1 child?
	check( RootGroup.Children.Num() == 1 );
	check( !RootGroup.Children[0].IsInstance() );
	check( RootGroup.Children[0].GetCluster( DstDAG ).NumExternalEdges == 0 );

	const uint32 DstClusterIndex = RootGroup.Children[0].ClusterIndex;

	// Add part's root clusters to the initial clusters of the next level
	for( uint32 i = 0; i < PartData.NumInstances; i++ )
	{
		uint32 InstanceIndex = PartData.FirstInstance + i;
		DstDAG.AssemblyInstanceData[ InstanceIndex ].RootClusterIndex = DstClusterIndex;
		DstDAG.AssemblyInstanceData[ InstanceIndex ].RootGroupIndex = DstDAG.Groups.Num() - 1;
		DstDAG.MeshInput[0].Add( FClusterRef( InstanceIndex, DstClusterIndex ) );
	}
}

static FUintVector2 AddAssemblyNodeBoneInfluences(const FNaniteAssemblyNode& Node, FClusterDAG& DAG)
{
	// Add bone influences to the flat list and retrieve the range for the part instance
	DAG.AssemblyBoneInfluences.Reserve(DAG.AssemblyBoneInfluences.Num() + Node.BoneInfluences.Num());
	uint32 FirstBoneInfluence = DAG.AssemblyBoneInfluences.Num();	
	uint32 NumBoneInfluences = 0;
	for (const FNaniteAssemblyBoneInfluence& Influence : Node.BoneInfluences)
	{
		if (!FMath::IsNearlyZero(Influence.BoneWeight))
		{
			if (NumBoneInfluences == NANITE_MAX_CLUSTER_BONE_INFLUENCES)
			{
				UE_LOGF(LogStaticMesh, Warning, "A Nanite Assembly part with too many bone influences encountered - clamped to %d.", NumBoneInfluences);
				break;
			}

			DAG.AssemblyBoneInfluences.Emplace(float(Influence.BoneIndex), Influence.BoneWeight);
			++NumBoneInfluences;
		}
	}
	
	if (NumBoneInfluences == 0)
	{
		return FUintVector2(MAX_uint32, 0);
	}

	// Normalize and pre-quantize the bone weights because we want to match the GPU for calculations from here
	TArrayView<FVector2f> BoneInfluences(&DAG.AssemblyBoneInfluences[FirstBoneInfluence], NumBoneInfluences);
	QuantizeAndSortBoneInfluenceWeights(BoneInfluences, uint32(255.0f));

	for (uint32 i = 0; i + NumBoneInfluences <= FirstBoneInfluence; ++i)
	{
		if (CompareItems(&DAG.AssemblyBoneInfluences[i], BoneInfluences.GetData(), NumBoneInfluences))
		{
			// identical set found
			DAG.AssemblyBoneInfluences.SetNum(FirstBoneInfluence, EAllowShrinking::No);
			FirstBoneInfluence = i;
			break;
		}
	}

	// When the DAG simplifies assembly part root clusters, their verts bone influences become those of their instance,
	// so push out the max of the entire DAG here
	DAG.MaxBoneInfluences = FMath::Max(DAG.MaxBoneInfluences, uint8(NumBoneInfluences));

	return FUintVector2(FirstBoneInfluence, NumBoneInfluences);
}

bool AddAssemblyParts( FIntermediateResources& AssemblyResources, const FInputAssemblyData& AssemblyData )
{
	const int32 NumInputParts = AssemblyData.Parts.Num();
	const int32 NumAssemblyNodes = AssemblyData.Nodes.Num();
	TArray<FMatrix44f> NodeTransforms;
	TArray<FUintVector2> NodeBoneInfluences;
	TArray<TArray<uint32>> NodeIndicesPerPart;
	TArray<uint32> PartsToMerge;
	bool bWarnNodesWithNoBoneInfluence = false;

	FClusterDAG& DAG = AssemblyResources.ClusterDAG;

	if( DAG.MeshInput.IsEmpty() )
		DAG.MeshInput.AddDefaulted();

	NodeTransforms.Reserve(NumAssemblyNodes);
	NodeIndicesPerPart.AddDefaulted(NumInputParts);
	if (DAG.bHasSkinning)
	{
		NodeBoneInfluences.AddUninitialized(NumAssemblyNodes);
	}

	// Flatten the input hierarchy and create transform lists by part
	for (int32 NodeIndex = 0; NodeIndex < NumAssemblyNodes; ++NodeIndex)
	{
		const auto& Node = AssemblyData.Nodes[NodeIndex];
			
		check(AssemblyData.Parts.IsValidIndex(Node.PartIndex));

		NodeIndicesPerPart[Node.PartIndex].Add(NodeIndex);

		FMatrix44f& NodeTransform = NodeTransforms.Emplace_GetRef(Node.Transform.ToMatrixWithScale());

		// Ignore the parent node's transform for skinned assemblies.
		if (DAG.bHasSkinning)
		{
			FUintVector2 BoneInfluences = AddAssemblyNodeBoneInfluences(Node, DAG);
			NodeBoneInfluences[NodeIndex] = BoneInfluences;
			bWarnNodesWithNoBoneInfluence |= (BoneInfluences.Y == 0);
			
			if (BoneInfluences.Y > 0 && Node.TransformSpace == ENaniteAssemblyNodeTransformSpace::BoneRelative)
			{
				// Calculate the weighted bind pose and apply it to the transform
				FMatrix44f BindPose { ForceInitToZero };
				for (uint32 i = 0; i < BoneInfluences.Y; ++i)
				{
					const FVector2f& Influence = DAG.AssemblyBoneInfluences[BoneInfluences.X + i];
					const uint32 BoneIndex = uint32(Influence.X);
					const float BoneWeight = Influence.Y / 255.0f;
					const FMatrix44f BonePoseMatrix = AssemblyData.ComposedRefPose.IsValidIndex(BoneIndex) ?
						AssemblyData.ComposedRefPose[BoneIndex] : FMatrix44f::Identity;
					BindPose += BonePoseMatrix * BoneWeight;
				}
				NodeTransform *= BindPose;
			}
		}
	}

	if (bWarnNodesWithNoBoneInfluence)
	{
		UE_LOGF(LogStaticMesh, Warning, "A skinned Nanite Assembly was encountered with 1 or more parts that have no bone influences - this can lead to rendering artifacts.");
	}

	// Remap assembly bone influences from skeleton-space to unified-space when the containing
	// skeletal mesh uses unified bone indices. The bind pose calculation above runs in skeleton
	// space (using AssemblyData.ComposedRefPose), so we deliberately defer this remap until
	// after that's done. Downstream consumers (vertex bone buffer encoding, fallback decode)
	// expect indices in the same space as the rest of the encoded data — unified.
	if (DAG.bHasSkinning && AssemblyData.SkeletonToUnifiedBoneIndex.Num() > 0)
	{
		const TConstArrayView<uint16> SkeletonToUnified = AssemblyData.SkeletonToUnifiedBoneIndex;
		bool bWarnInvalidMapping = false;
		for (FVector2f& Influence : DAG.AssemblyBoneInfluences)
		{
			const uint32 SkeletonBoneIndex = uint32(Influence.X);
			if (SkeletonToUnified.IsValidIndex(SkeletonBoneIndex))
			{
				const uint16 UnifiedIndex = SkeletonToUnified[SkeletonBoneIndex];
				if (UnifiedIndex != uint16(INDEX_NONE))
				{
					Influence.X = float(UnifiedIndex);
				}
				else
				{
					// Skeleton bone not in unified map; clamp to root with zero weight.
					Influence.X = 0.0f;
					Influence.Y = 0.0f;
					bWarnInvalidMapping = true;
				}
			}
			else
			{
				Influence.X = 0.0f;
				Influence.Y = 0.0f;
				bWarnInvalidMapping = true;
			}
		}
		if (bWarnInvalidMapping)
		{
			UE_LOGF(LogStaticMesh, Warning, "Nanite Assembly contained one or more bone influences with no corresponding unified bone index. Affected influences were clamped to bone 0 with zero weight.");
		}
	}

	// Merge parts' clusters into the assembly output, save the last mip level off for the mip tail.
	for (int32 InputPartIndex = 0; InputPartIndex < NumInputParts; ++InputPartIndex)
	{
		const FIntermediateResources& PartResources = *AssemblyData.Parts[InputPartIndex].Resource;		

		const int32 NumNodesInPart = NodeIndicesPerPart[InputPartIndex].Num();
		if (NumNodesInPart == 0)
		{
			// Don't need to merge this part (Should this be an error condition?)
			continue;
		}

		if (!PartResources.ClusterDAG.AssemblyPartData.IsEmpty())
		{
			// Not currently handled; unclear how to identify and de-duplicate common inner clusters
			// TODO: Nanite-Assemblies: Assemblies of assemblies
			UE_LOGF(LogStaticMesh, Warning, "Failed to build Nanite assembly. Assemblies of assemblies is not currently supported.");
			return false;
		}

		const TArray<uint32>& PartNodeIndices = NodeIndicesPerPart[InputPartIndex];
		const FAssemblyPartData& NewPart = DAG.AssemblyPartData.Emplace_GetRef( DAG.AssemblyInstanceData.Num(), PartNodeIndices.Num() );

		for (uint32 NodeIndex : PartNodeIndices)
		{
			const auto& Node = AssemblyData.Nodes[NodeIndex];
			uint32 FirstBoneInfluence = INDEX_NONE;
			uint32 NumBoneInfluences = 0;
			if (NodeBoneInfluences.IsValidIndex(NodeIndex))
			{
				FirstBoneInfluence = NodeBoneInfluences[NodeIndex].X;
				NumBoneInfluences = NodeBoneInfluences[NodeIndex].Y;
			}

			DAG.AssemblyInstanceData.Emplace( 
				NodeTransforms[NodeIndex],
				0, // LODBounds
				-1.0f, // ParentLODError
				MAX_uint32, // RootClusterIndex
				MAX_uint32, // RootGroupIndex
				FirstBoneInfluence,
				NumBoneInfluences
			);
		}

		AddAssemblyPart(
			AssemblyResources,
			PartResources,
			AssemblyData.Parts[InputPartIndex].MaterialRemap,
			DAG.AssemblyPartData.Num() - 1
		);
	}

	// Error out if the resulting transform count is too large
	const int32 NumFinalTransforms = DAG.AssemblyInstanceData.Num();
	if (NumFinalTransforms > NANITE_HIERARCHY_MAX_ASSEMBLY_TRANSFORMS)
	{
		UE_LOGF(LogStaticMesh, Error,
			"Merged Nanite assembly has too many transforms (%d). Max is %d.",
			NumFinalTransforms, NANITE_HIERARCHY_MAX_ASSEMBLY_TRANSFORMS);
		return false;
	}

	return true;
}

} // namespace Nanite
