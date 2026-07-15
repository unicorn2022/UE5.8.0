// Copyright Epic Games, Inc. All Rights Reserved.

#include "Item/MetaHumanCrowdGroomEditorPipeline.h"

#include "Item/MetaHumanCrowdGroomPipeline.h"
#include "Item/MetaHumanMaterialPipelineCommon.h"
#include "MetaHumanCrowdEditorLog.h"
#include "MetaHumanCrowdEditorUtilities.h"
#include "MetaHumanCrowdTypes.h"
#include "MetaHumanHashUtilities.h"
#include "MetaHumanWardrobeItem.h"
#include "Item/MetaHumanDefaultGroomPipeline.h"

#include "Logging/StructuredLog.h"
#include "GroomBindingAsset.h"
#include "GroomAsset.h"
#include "GroomRBFDeformer.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "AssetCompilingManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "Materials/MaterialInstanceConstant.h"
#include "StaticParameterSet.h"
#include "Engine/Texture2D.h"
#include "MeshDescription.h"
#include "StaticMeshOperations.h"
#include "StaticMeshAttributes.h"
#include "SkeletalMeshAttributes.h"
#include "Math/NumericLimits.h"
#include "Async/ParallelFor.h"
#include "Templates/Function.h"
#include "Containers/ArrayView.h"
#include "DerivedDataCache.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataRequestOwner.h"
#include "DerivedDataRequestTypes.h"
#include "DerivedDataValueId.h"
#include "IO/IoHash.h"
#include "Serialization/MemoryReader.h"

namespace UE::MetaHuman::Private
{
	// Bump this GUID whenever the groom fitting logic changes in a way that invalidates cached results.
	static const FGuid GroomFittingDDCVersion(0x7F2B91D4, 0x5A6E3C08, 0x49E174BC, 0xD3088A2F);

	static UE::DerivedData::FCacheKey MakeGroomFittingCacheKey(
		const FString& TargetMeshDerivedDataKey,
		TNotNull<const UGroomBindingAsset*> GroomBinding,
		TNotNull<const UGroomAsset*> GroomAsset,
		const FGroomCardConversionParams& ConversionParams,
		TNotNull<const UMaterialInterface*> CardsMaterial,
		const FGuid& BuildCacheGuid)
	{
		using namespace UE::DerivedData;

		FIoHashBuilder KeyHashBuilder;
		KeyHashBuilder.Update(&GroomFittingDDCVersion, sizeof(GroomFittingDDCVersion));
		KeyHashBuilder.Update(TargetMeshDerivedDataKey.GetCharArray().GetData(), TargetMeshDerivedDataKey.GetCharArray().Num() * sizeof(TCHAR));

		const FIoHash BindingHash = HashUtilities::HashUObject(GroomBinding);
		KeyHashBuilder.Update(&BindingHash, sizeof(BindingHash));

		const FIoHash GroomHash = HashUtilities::HashUObject(GroomAsset);
		KeyHashBuilder.Update(&GroomHash, sizeof(GroomHash));

		const FIoHash ParamsHash = HashUtilities::HashUStruct(FGroomCardConversionParams::StaticStruct(), &ConversionParams);
		KeyHashBuilder.Update(&ParamsHash, sizeof(ParamsHash));

		const FIoHash MaterialHash = HashUtilities::HashUObject(CardsMaterial);
		KeyHashBuilder.Update(&MaterialHash, sizeof(MaterialHash));

		KeyHashBuilder.Update(&BuildCacheGuid, sizeof(BuildCacheGuid));

		FCacheKey CacheKey;
		CacheKey.Bucket = FCacheBucket(TEXT("MetaHumanGroomFitting"));
		CacheKey.Hash = KeyHashBuilder.Finalize();
		return CacheKey;
	}

	static const UE::DerivedData::FValueId GroomMeshDescriptionsValueId = UE::DerivedData::FValueId::FromName("GroomMeshDescriptions");

	static FString MakeGroomFittingCacheRecordName(const USkeletalMesh* TargetBodyMesh)
	{
		return FString::Format(TEXT("MetaHumanCrowdGroomFitting_{0}"), { TargetBodyMesh->GetPathName() });
	}

	static void SerializeGroomMeshDescriptions(
		FArchive& Ar,
		TArray<FMeshDescription>& MeshDescriptions)
	{
		int32 NumLODs = MeshDescriptions.Num();
		Ar << NumLODs;

		if (Ar.IsLoading())
		{
			MeshDescriptions.SetNum(NumLODs);
		}

		for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
		{
			Ar << MeshDescriptions[LODIndex];
		}
	}

	// Serializes the MeshDescriptions from a fitted groom into the DDC.
	static void StoreGroomBundleInDDC(
		const UE::DerivedData::FCacheKey& CacheKey,
		TArray<FMeshDescription>& MeshDescriptions,
		const USkeletalMesh* TargetBodyMesh)
	{
		using namespace UE::DerivedData;

		ICache* Cache = TryGetCache();
		if (!Cache)
		{
			return;
		}

		TArray<uint8> SerializedData;
		FMemoryWriter Ar(SerializedData, /*bIsPersistent=*/ true);

		SerializeGroomMeshDescriptions(Ar, MeshDescriptions);

		FCacheRecordBuilder RecordBuilder(CacheKey);
		FValue Value = FValue::Compress(FSharedBuffer::MakeView(SerializedData.GetData(), SerializedData.Num()));
		RecordBuilder.AddValue(GroomMeshDescriptionsValueId, MoveTemp(Value));

		FRequestOwner RequestOwner(EPriority::Normal);
		FCachePutRequest PutRequest = {
			FSharedString(MakeGroomFittingCacheRecordName(TargetBodyMesh)),
			RecordBuilder.Build(),
			ECachePolicy::Default
		};
		Cache->Put(MakeArrayView(&PutRequest, 1), RequestOwner, [](FCachePutResponse&&) {});
		RequestOwner.Wait();
	}

	// Attempts to load cached MeshDescriptions from the DDC and populate a geometry bundle.
	// Materials are not cached; the caller is responsible for setting them after a successful load.
	// RefSkeleton comes from the target body mesh since the groom fitting does not alter bone poses.
	static bool TryLoadGroomBundleFromDDC(
		const UE::DerivedData::FCacheKey& CacheKey,
		const USkeletalMesh* TargetBodyMesh,
		FMetaHumanCrowdMeshGeometryBundle& OutBundle)
	{
		using namespace UE::DerivedData;

		ICache* Cache = TryGetCache();
		if (!Cache)
		{
			return false;
		}

		FSharedBuffer CachedData;
		FCacheGetRequest Request;
		Request.Name = MakeGroomFittingCacheRecordName(TargetBodyMesh);
		Request.Key = CacheKey;
		Request.Policy = ECachePolicy::Default;

		FRequestOwner RequestOwner(EPriority::Blocking);
		Cache->Get(MakeArrayView(&Request, 1), RequestOwner,
			[&CachedData](FCacheGetResponse&& Response)
			{
				if (Response.Status == EStatus::Ok)
				{
					const FCompressedBuffer& CompressedBuffer = Response.Record.GetValue(GroomMeshDescriptionsValueId).GetData();
					CachedData = CompressedBuffer.Decompress();
				}
			});
		RequestOwner.Wait();

		if (CachedData.IsNull())
		{
			return false;
		}

		FMemoryReaderView Ar(CachedData.GetView(), /*bIsPersistent=*/ true);

		SerializeGroomMeshDescriptions(Ar, OutBundle.MeshDescriptions);
		OutBundle.RefSkeleton = TargetBodyMesh->GetRefSkeleton();
		// Materials are not cached -- caller sets them after this returns.

		return true;
	}

	/**
	 * Assigns all vertices to a single bone with full weight.
	 *
	 * @param InOutTargetMeshDesc Mesh Description to update
	 * @param InTargetBoneName Bone to assign all vertices to
	 * @return True if successful, false otherwise
	 */
	bool AssignAllVerticesToBone(
		FMeshDescription& InOutTargetMeshDesc,
		FName InTargetBoneName)
	{
		if (InTargetBoneName.IsNone())
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning,
				"AssignAllVerticesToBone: TargetBoneName is None. Set TargetBoneName on the groom pipeline to enable SingleBone skin-weight transfer");
			return false;
		}

		FSkeletalMeshAttributes TargetAttribs(InOutTargetMeshDesc);
		FSkinWeightsVertexAttributesRef TargetSW = TargetAttribs.GetVertexSkinWeights();
		FSkeletalMeshAttributes::FBoneNameAttributesConstRef BoneNames = TargetAttribs.GetBoneNames();

		if (!TargetSW.IsValid())
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "AssignAllVerticesToBone: target MeshDescription has no skin weight attributes");
			return false;
		}

		if (!BoneNames.IsValid())
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "AssignAllVerticesToBone: target MeshDescription has no bone name attributes");
			return false;
		}

		int32 TargetBoneID = INDEX_NONE;
		if (BoneNames.IsValid())
		{
			for (const FBoneID BoneID : TargetAttribs.Bones().GetElementIDs())
			{
				if (BoneNames.Get(BoneID) == InTargetBoneName)
				{
					TargetBoneID = BoneID.GetValue();
					break;
				}
			}
		}

		if (TargetBoneID == INDEX_NONE)
		{
			return false;
		}

		UE::AnimationCore::FBoneWeightsSettings Settings;
		Settings.SetNormalizeType(UE::AnimationCore::EBoneWeightNormalizeType::Always);
		const UE::AnimationCore::FBoneWeights FullWeight = UE::AnimationCore::FBoneWeights::Create(
			{ UE::AnimationCore::FBoneWeight(TargetBoneID, 1.0f) }, Settings);

		for (const FVertexID VID : InOutTargetMeshDesc.Vertices().GetElementIDs())
		{
			TargetSW.Set(VID, FullWeight);
		}

		return true;
	}

	/**
	 * Builds connected components from mesh triangles using union-find.
	 *
	 * @param InMeshDesc Mesh description to analyze
	 * @param InIndexToVertexID Compact-index -> FVertexID mapping
	 * @param OutComponentIDs Output array mapping compact vertex index to component ID. Sized to InIndexToVertexID.Num().
	 */
	void BuildConnectedComponents(const FMeshDescription* InMeshDesc, TConstArrayView<FVertexID> InIndexToVertexID, TArray<int32>& OutComponentIDs)
	{
		const int32 NumVertices = InIndexToVertexID.Num();

		// Build the inverse mapping (FVertexID -> compact index) for triangle-edge lookups.
		TMap<FVertexID, int32> VertexIDToIndex;
		VertexIDToIndex.Reserve(NumVertices);
		for (int32 CompactIndex = 0; CompactIndex < NumVertices; ++CompactIndex)
		{
			VertexIDToIndex.Add(InIndexToVertexID[CompactIndex], CompactIndex);
		}

		// Union-Find data structure
		TArray<int32> Parent;
		TArray<int32> Rank;
		Parent.SetNumUninitialized(NumVertices);
		Rank.SetNumZeroed(NumVertices);

		// Initialize: each vertex is its own parent
		for (int32 i = 0; i < NumVertices; ++i)
		{
			Parent[i] = i;
		}

		// Find with path compression
		auto Find = [&Parent](int32 x) -> int32
		{
			// Iterative path compression
			int32 Root = x;
			while (Parent[Root] != Root)
			{
				Root = Parent[Root];
			}
			// Compress path
			while (Parent[x] != Root)
			{
				int32 Next = Parent[x];
				Parent[x] = Root;
				x = Next;
			}
			return Root;
		};

		// Union by rank
		auto Unite = [&Parent, &Rank, &Find](int32 x, int32 y)
		{
			int32 RootX = Find(x);
			int32 RootY = Find(y);

			if (RootX != RootY)
			{
				if (Rank[RootX] < Rank[RootY])
				{
					Parent[RootX] = RootY;
				}
				else if (Rank[RootX] > Rank[RootY])
				{
					Parent[RootY] = RootX;
				}
				else
				{
					Parent[RootY] = RootX;
					Rank[RootX]++;
				}
			}
		};

		// Unite vertices that share triangle edges
		for (const FTriangleID TriangleID : InMeshDesc->Triangles().GetElementIDs())
		{
			TArrayView<const FVertexInstanceID> VertexInstanceIDs = InMeshDesc->GetTriangleVertexInstances(TriangleID);

			const int32 V0 = VertexIDToIndex[InMeshDesc->GetVertexInstanceVertex(VertexInstanceIDs[0])];
			const int32 V1 = VertexIDToIndex[InMeshDesc->GetVertexInstanceVertex(VertexInstanceIDs[1])];
			const int32 V2 = VertexIDToIndex[InMeshDesc->GetVertexInstanceVertex(VertexInstanceIDs[2])];

			Unite(V0, V1);
			Unite(V1, V2);
			Unite(V2, V0);
		}

		// Normalize component IDs to be contiguous (0, 1, 2, ...)
		TMap<int32, int32> RootToComponentID;
		OutComponentIDs.SetNumUninitialized(NumVertices);

		int32 NextComponentID = 0;
		for (int32 i = 0; i < NumVertices; ++i)
		{
			const int32 Root = Find(i);

			if (int32* ExistingID = RootToComponentID.Find(Root))
			{
				OutComponentIDs[i] = *ExistingID;
			}
			else
			{
				RootToComponentID.Add(Root, NextComponentID);
				OutComponentIDs[i] = NextComponentID;
				NextComponentID++;
			}
		}
	}

	/**
	 * Source mesh triangle: positions and source-vertex indices for the three corners.
	 */
	struct FSourceTriangle
	{
		FVector P0, P1, P2;
		int32 V0, V1, V2;
	};

	/**
	 * Per target-vertex closest-triangle info, populated by BuildCardConversionContext.
	 */
	struct FVertexClosestInfo
	{
		float DistSq = TNumericLimits<float>::Max();
		int32 TriangleIndex = 0;
		FVector BarycentricCoords = FVector(1.0f, 0.0f, 0.0f);
	};

	/**
	 * Shared work product for any closest-point-based skin-weight transfer.
	 *
	 *   - SourceTriangles: filtered source triangles (excluded slots dropped, optionally
	 *     bone-influence-filtered against TargetBoneName + descendants)
	 *   - ValidBoneIndices: TargetBoneName + its descendants. Empty when no filter is active.
	 *     Used at both the triangle-skip step (during build) and the weight-read step
	 *     (during interpolation) to keep weights from off-target bones (e.g. shoulder/spine)
	 *     out of the result, even when those weights live on a corner of an otherwise valid
	 *     triangle.
	 *   - IndexToVertexID: compact target vertex index -> FVertexID (FVertexIDs can be sparse)
	 *   - VertexClosestInfo: parallel result of closest-triangle search per target vertex
	 *
	 * Sized so VertexClosestInfo[i] corresponds to IndexToVertexID[i].
	 */
	struct FCardConversionContext
	{
		TArray<FSourceTriangle> SourceTriangles;
		TSet<int32> ValidBoneIndices;
		TArray<FVertexID> IndexToVertexID;
		TArray<FVertexClosestInfo> VertexClosestInfo;
	};

	/**
	 * Read bone-name -> weight pairs for a single source-mesh vertex, optionally filtered
	 * to the set of bone indices in InValidBoneIndices. When InValidBoneIndices is empty,
	 * no filtering is applied (all weights are returned).
	 */
	TMap<FName, float> GetSourceVertexWeights(
		const FSkinWeightsVertexAttributesConstRef& InSourceSkinWeights,
		const FReferenceSkeleton& InRefSkeleton,
		const TSet<int32>& InValidBoneIndices,
		int32 InVertexIndex)
	{
		TMap<FName, float> Weights;
		const bool bFilterActive = !InValidBoneIndices.IsEmpty();
		for (UE::AnimationCore::FBoneWeight BoneWeight : InSourceSkinWeights.Get(FVertexID(InVertexIndex)))
		{
			const int32 BoneIdx = BoneWeight.GetBoneIndex();
			if (bFilterActive && !InValidBoneIndices.Contains(BoneIdx))
			{
				continue;
			}
			const FName BoneName = InRefSkeleton.GetBoneName(BoneIdx);
			Weights.Add(BoneName, BoneWeight.GetWeight());
		}
		return Weights;
	}

	/**
	 * Builds an FCardConversionContext: validates the source mesh, filters source triangles
	 * (by excluded material slots and optional target-bone influence), enumerates the target
	 * vertices, and runs a parallel closest-triangle search for every target vertex.
	 *
	 * Logs a warning and returns false if any required attribute is missing or the filtered
	 * triangle list is empty.
	 */
	bool BuildCardConversionContext(
		TNotNull<const USkeletalMesh*> InSourceSkelMesh,
		const FMeshDescription& InTargetMeshDesc,
		const FGroomCardConversionParams& InParams,
		int32 InSourceLODIndex,
		FCardConversionContext& OutContext)
	{
		if (!InSourceSkelMesh->IsValidLODIndex(InSourceLODIndex))
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Error,
				"BuildCardConversionContext: LOD {LOD} out of range on source mesh '{Mesh}' (has {NumLODs} LODs).",
				InSourceLODIndex, InSourceSkelMesh->GetName(), InSourceSkelMesh->GetLODNum());
			return false;
		}

		const FMeshDescription* SourceMeshDesc = InSourceSkelMesh->GetMeshDescription(InSourceLODIndex);

		if (!SourceMeshDesc || SourceMeshDesc->Vertices().Num() == 0)
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Error,
				"BuildCardConversionContext: source mesh '{Mesh}' has no MeshDescription or vertices at LOD {LOD}.",
				InSourceSkelMesh->GetName(), InSourceLODIndex);
			return false;
		}

		FSkeletalMeshConstAttributes SourceMeshAttribs(*SourceMeshDesc);
		FSkinWeightsVertexAttributesConstRef SourceSkinWeights = SourceMeshAttribs.GetVertexSkinWeights();
		const FReferenceSkeleton& RefSkeleton = InSourceSkelMesh->GetRefSkeleton();

		if (!SourceSkinWeights.IsValid())
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "BuildCardConversionContext: source mesh has no skin weight attributes");
			return false;
		}

		// Build set of excluded polygon group IDs based on material slot names
		TSet<FPolygonGroupID> ExcludedPolygonGroups;
		{
			const TPolygonGroupAttributesConstRef<FName> PolygonGroupMaterialSlotNames = SourceMeshAttribs.GetPolygonGroupMaterialSlotNames();

			for (const FPolygonGroupID PolygonGroupID : SourceMeshDesc->PolygonGroups().GetElementIDs())
			{
				const FName SlotName = PolygonGroupMaterialSlotNames[PolygonGroupID];
				if (InParams.ExcludedMaterialSlotNames.Contains(SlotName))
				{
					ExcludedPolygonGroups.Add(PolygonGroupID);
				}
			}
		}

		// Build set of valid bone indices (target bone and all its descendants). Empty when
		// no filter is active, in which case all triangles are considered. Stored on the
		// context so the weight-read step can apply the same filter.
		if (!InParams.TargetBoneName.IsNone())
		{
			const int32 TargetBoneIndex = RefSkeleton.FindBoneIndex(InParams.TargetBoneName);
			if (TargetBoneIndex != INDEX_NONE)
			{
				OutContext.ValidBoneIndices.Add(TargetBoneIndex);

				for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
				{
					int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
					while (ParentIndex != INDEX_NONE)
					{
						if (ParentIndex == TargetBoneIndex)
						{
							OutContext.ValidBoneIndices.Add(BoneIndex);
							break;
						}
						ParentIndex = RefSkeleton.GetParentIndex(ParentIndex);
					}
				}
			}
		}

		// Helper: does this source vertex have any significant weight on a valid bone?
		auto HasValidBoneInfluence = [&SourceSkinWeights, &OutContext, &InParams](int32 VertexIndex) -> bool
		{
			if (OutContext.ValidBoneIndices.IsEmpty())
			{
				return true; // No filter active - accept all
			}

			for (UE::AnimationCore::FBoneWeight BoneWeight : SourceSkinWeights.Get(FVertexID(VertexIndex)))
			{
				if (BoneWeight.GetWeight() > InParams.BlendWeightsThreshold &&
					OutContext.ValidBoneIndices.Contains(BoneWeight.GetBoneIndex()))
				{
					return true;
				}
			}
			return false;
		};

		// Build filtered source triangle list with positions cached
		for (const FTriangleID TriangleID : SourceMeshDesc->Triangles().GetElementIDs())
		{
			if (ExcludedPolygonGroups.Contains(SourceMeshDesc->GetTrianglePolygonGroup(TriangleID)))
			{
				continue;
			}

			TArrayView<const FVertexInstanceID> VertexInstanceIDs = SourceMeshDesc->GetTriangleVertexInstances(TriangleID);

			const FVertexID V0 = SourceMeshDesc->GetVertexInstanceVertex(VertexInstanceIDs[0]);
			const FVertexID V1 = SourceMeshDesc->GetVertexInstanceVertex(VertexInstanceIDs[1]);
			const FVertexID V2 = SourceMeshDesc->GetVertexInstanceVertex(VertexInstanceIDs[2]);

			if (!HasValidBoneInfluence(V0.GetValue()) &&
				!HasValidBoneInfluence(V1.GetValue()) &&
				!HasValidBoneInfluence(V2.GetValue()))
			{
				continue;
			}

			OutContext.SourceTriangles.Add({
				FVector(SourceMeshDesc->GetVertexPosition(V0)),
				FVector(SourceMeshDesc->GetVertexPosition(V1)),
				FVector(SourceMeshDesc->GetVertexPosition(V2)),
				V0.GetValue(),
				V1.GetValue(),
				V2.GetValue()
			});
		}

		if (OutContext.SourceTriangles.IsEmpty())
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "BuildCardConversionContext: no source triangles passed material/bone-influence filtering");
			return false;
		}

		// Enumerate target vertices into a compact index array (FVertexIDs can be sparse).
		const FVertexArray& TargetVertices = InTargetMeshDesc.Vertices();
		const int32 TargetNumVertices = TargetVertices.Num();
		OutContext.IndexToVertexID.Reserve(TargetNumVertices);
		for (const FVertexID VertexID : TargetVertices.GetElementIDs())
		{
			OutContext.IndexToVertexID.Add(VertexID);
		}

		if (OutContext.IndexToVertexID.Num() == 0)
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "BuildCardConversionContext: target mesh has no vertices");
			return false;
		}

		// Parallel search for each target vertex's closest source triangle and barycentric coords
		OutContext.VertexClosestInfo.SetNum(OutContext.IndexToVertexID.Num());

		ParallelFor(TEXT("MetaHuman Crowd Groom: Closest Triangle Search"), OutContext.IndexToVertexID.Num(), 1, [&InTargetMeshDesc, &OutContext](int32 CompactIndex)
		{
			const FVector VertexPos = FVector(InTargetMeshDesc.GetVertexPosition(OutContext.IndexToVertexID[CompactIndex]));

			float MinDistSq = TNumericLimits<float>::Max();
			int32 ClosestTriIdx = 0;
			FVector ClosestBary = FVector(1.0f, 0.0f, 0.0f);

			for (int32 TriIdx = 0; TriIdx < OutContext.SourceTriangles.Num(); ++TriIdx)
			{
				const FSourceTriangle& Tri = OutContext.SourceTriangles[TriIdx];
				const FVector ClosestPoint = FMath::ClosestPointOnTriangleToPoint(VertexPos, Tri.P0, Tri.P1, Tri.P2);
				const float DistSq = FVector::DistSquared(VertexPos, ClosestPoint);

				if (DistSq < MinDistSq)
				{
					MinDistSq = DistSq;
					ClosestTriIdx = TriIdx;
					ClosestBary = FMath::GetBaryCentric2D(ClosestPoint, Tri.P0, Tri.P1, Tri.P2);
				}
			}

			OutContext.VertexClosestInfo[CompactIndex] = { MinDistSq, ClosestTriIdx, ClosestBary };
		});

		return true;
	}

	/**
	 * Writes per-vertex skin weights into a target MeshDescription, normalizing each row.
	 * The caller supplies a callable returning the bone-weight map for each compact vertex
	 * index - this avoids materialising a per-vertex TMap when the underlying weights are
	 * actually shared (e.g. one-per-strand in StrandBased).
	 *
	 * @param InOutTargetMeshDesc Mesh description to update.
	 * @param InIndexToVertexID Compact index -> FVertexID mapping
	 * @param InGetWeightsForVertex Callable: (CompactIndex) -> const TMap<FName,float>& of bone weights.
	 * @return True on success, false if target attributes are missing.
	 */
	bool WriteSkinWeightsToTarget(
		FMeshDescription& InOutTargetMeshDesc,
		TConstArrayView<FVertexID> InIndexToVertexID,
		TFunctionRef<const TMap<FName, float>& (int32)> InGetWeightsForVertex)
	{
		FSkeletalMeshAttributes TargetAttribs(InOutTargetMeshDesc);
		FSkinWeightsVertexAttributesRef TargetSW = TargetAttribs.GetVertexSkinWeights();
		FSkeletalMeshAttributes::FBoneNameAttributesConstRef TargetBoneNames = TargetAttribs.GetBoneNames();

		if (!TargetSW.IsValid())
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "WriteSkinWeightsToTarget: target MeshDescription has no skin weight attributes");
			return false;
		}

		if (!TargetBoneNames.IsValid())
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "WriteSkinWeightsToTarget: target MeshDescription has no bone name attributes");
			return false;
		}

		TMap<FName, int32> BoneNameToID;
		for (const FBoneID BoneID : TargetAttribs.Bones().GetElementIDs())
		{
			BoneNameToID.Add(TargetBoneNames.Get(BoneID), BoneID.GetValue());
		}

		UE::AnimationCore::FBoneWeightsSettings Settings;
		Settings.SetNormalizeType(UE::AnimationCore::EBoneWeightNormalizeType::Always);

		const int32 NumVertices = InIndexToVertexID.Num();
		for (int32 CompactIndex = 0; CompactIndex < NumVertices; ++CompactIndex)
		{
			const TMap<FName, float>& Weights = InGetWeightsForVertex(CompactIndex);
			TArray<UE::AnimationCore::FBoneWeight> BWArray;
			BWArray.Reserve(Weights.Num());

			for (const TPair<FName, float>& Pair : Weights)
			{
				if (const int32* BID = BoneNameToID.Find(Pair.Key))
				{
					BWArray.Add(UE::AnimationCore::FBoneWeight(*BID, Pair.Value));
				}
			}

			TargetSW.Set(InIndexToVertexID[CompactIndex], UE::AnimationCore::FBoneWeights::Create(BWArray, Settings));
		}

		return true;
	}

	/**
	 * Copies skin weights using a strand-based approach.
	 *
	 * Identifies strands via connected components, picks the vertex closest to the source
	 * mesh as the anchor for each strand, and assigns the anchor's barycentric-interpolated
	 * weights uniformly to every vertex in that strand. Yields rigid per-strand binding,
	 * which is appropriate for hair cards where each strand should move as a unit.
	 *
	 * Triangle filtering is delegated to BuildCardConversionContext: ExcludedMaterialSlotNames
	 * drops source polygon groups, and TargetBoneName (if set) restricts source triangles to
	 * those influenced by that bone or its descendants - preventing long strands from picking
	 * up shoulder/spine weights when their tips hang near the body.
	 *
	 * @param InSourceSkelMesh Source skeletal mesh to copy weights from (e.g., face)
	 * @param InOutTargetMeshDesc Mesh Description to update
	 * @param InParams Parameters controlling weight transfer
	 * @param InSourceLODIndex LOD index to copy skin weights from
	 * @return True if successful, false otherwise
	 */
	bool CopySkinWeightsStrandBased(
		TNotNull<const USkeletalMesh*> InSourceSkelMesh,
		FMeshDescription& InOutTargetMeshDesc,
		const FGroomCardConversionParams& InParams,
		int32 InSourceLODIndex)
	{
		FCardConversionContext Context;
		if (!BuildCardConversionContext(InSourceSkelMesh, InOutTargetMeshDesc, InParams, InSourceLODIndex, Context))
		{
			return false;
		}

		const FMeshDescription* SourceMeshDesc = InSourceSkelMesh->GetMeshDescription(InSourceLODIndex);
		FSkinWeightsVertexAttributesConstRef SourceSkinWeights = FSkeletalMeshConstAttributes(*SourceMeshDesc).GetVertexSkinWeights();
		const FReferenceSkeleton& RefSkeleton = InSourceSkelMesh->GetRefSkeleton();

		// Group target vertices into connected components ("strands").
		TArray<int32> ComponentIDs;
		BuildConnectedComponents(&InOutTargetMeshDesc, Context.IndexToVertexID, ComponentIDs);

		const int32 TargetNumVertices = Context.IndexToVertexID.Num();

		int32 NumComponents = 0;
		for (int32 CompID : ComponentIDs)
		{
			NumComponents = FMath::Max(NumComponents, CompID + 1);
		}

		TArray<TArray<int32>> ComponentVertices;
		ComponentVertices.SetNum(NumComponents);
		for (int32 CompactIndex = 0; CompactIndex < TargetNumVertices; ++CompactIndex)
		{
			ComponentVertices[ComponentIDs[CompactIndex]].Add(CompactIndex);
		}

		// For each strand, pick the anchor (vertex closest to source mesh) and compute the
		// barycentric-interpolated weights at the anchor's projection onto its closest source
		// triangle. Those weights apply to every vertex in the strand.
		struct FStrandWeights
		{
			TMap<FName, float> Weights;
		};
		TArray<FStrandWeights> StrandWeights;
		StrandWeights.SetNum(NumComponents);

		ParallelFor(TEXT("MetaHuman Crowd Groom: Strand Weight Resolution"), NumComponents, 1, [&](int32 ComponentIndex)
		{
			const TArray<int32>& VerticesInStrand = ComponentVertices[ComponentIndex];

			if (VerticesInStrand.IsEmpty())
			{
				return;
			}

			// Find anchor: vertex in strand with the minimum distance to the source mesh.
			int32 AnchorVertexID = VerticesInStrand[0];
			float MinDistSq = Context.VertexClosestInfo[AnchorVertexID].DistSq;

			for (int32 VertexID : VerticesInStrand)
			{
				if (Context.VertexClosestInfo[VertexID].DistSq < MinDistSq)
				{
					MinDistSq = Context.VertexClosestInfo[VertexID].DistSq;
					AnchorVertexID = VertexID;
				}
			}

			const FVertexClosestInfo& AnchorInfo = Context.VertexClosestInfo[AnchorVertexID];
			const FSourceTriangle& AnchorTri = Context.SourceTriangles[AnchorInfo.TriangleIndex];
			const FVector& ClosestBary = AnchorInfo.BarycentricCoords;

			const TMap<FName, float> Weights0 = GetSourceVertexWeights(SourceSkinWeights, RefSkeleton, Context.ValidBoneIndices, AnchorTri.V0);
			const TMap<FName, float> Weights1 = GetSourceVertexWeights(SourceSkinWeights, RefSkeleton, Context.ValidBoneIndices, AnchorTri.V1);
			const TMap<FName, float> Weights2 = GetSourceVertexWeights(SourceSkinWeights, RefSkeleton, Context.ValidBoneIndices, AnchorTri.V2);

			TSet<FName> AllBones;
			for (const TPair<FName, float>& Pair : Weights0)
			{
				AllBones.Add(Pair.Key);
			}
			for (const TPair<FName, float>& Pair : Weights1)
			{
				AllBones.Add(Pair.Key);
			}
			for (const TPair<FName, float>& Pair : Weights2)
			{
				AllBones.Add(Pair.Key);
			}

			TMap<FName, float>& InterpolatedWeights = StrandWeights[ComponentIndex].Weights;
			for (FName BoneName : AllBones)
			{
				const float W0 = Weights0.Contains(BoneName) ? Weights0[BoneName] : 0.0f;
				const float W1 = Weights1.Contains(BoneName) ? Weights1[BoneName] : 0.0f;
				const float W2 = Weights2.Contains(BoneName) ? Weights2[BoneName] : 0.0f;

				const float InterpolatedWeight = W0 * ClosestBary.X + W1 * ClosestBary.Y + W2 * ClosestBary.Z;

				if (InterpolatedWeight > InParams.BlendWeightsThreshold)
				{
					InterpolatedWeights.Add(BoneName, InterpolatedWeight);
				}
			}
		});

		// Write strand weights to every vertex via a callable that maps compact index -> strand.
		// No per-vertex broadcast - each strand's TMap is read directly for every vertex in it.
		return WriteSkinWeightsToTarget(
			InOutTargetMeshDesc,
			Context.IndexToVertexID,
			[&StrandWeights, &ComponentIDs](int32 CompactIndex) -> const TMap<FName, float>&
			{
				return StrandWeights[ComponentIDs[CompactIndex]].Weights;
			});
	}

	/**
	 * Copies skin weights using a per-vertex closest-point approach.
	 *
	 * For each target vertex: find the closest point on the closest source triangle (already
	 * computed by BuildCardConversionContext), then barycentric-interpolate the three corner
	 * vertices' bone weights at that point and assign the result. Unlike StrandBased, this
	 * treats every target vertex independently - no connected-component grouping, no shared
	 * anchor - so the resulting weights vary smoothly across the cards/helmet surface.
	 *
	 * Same triangle-filtering rules as StrandBased (handled by BuildCardConversionContext).
	 *
	 * @param InSourceSkelMesh Source skeletal mesh to copy weights from (e.g., face)
	 * @param InOutTargetMeshDesc Mesh Description to update
	 * @param InParams Parameters controlling weight transfer
	 * @param InSourceLODIndex LOD index to copy skin weights from
	 * @return True if successful, false otherwise
	 */
	bool CopySkinWeightsPerVertex(
		TNotNull<const USkeletalMesh*> InSourceSkelMesh,
		FMeshDescription& InOutTargetMeshDesc,
		const FGroomCardConversionParams& InParams,
		int32 InSourceLODIndex)
	{
		FCardConversionContext Context;
		if (!BuildCardConversionContext(InSourceSkelMesh, InOutTargetMeshDesc, InParams, InSourceLODIndex, Context))
		{
			return false;
		}

		const FMeshDescription* SourceMeshDesc = InSourceSkelMesh->GetMeshDescription(InSourceLODIndex);
		FSkinWeightsVertexAttributesConstRef SourceSkinWeights = FSkeletalMeshConstAttributes(*SourceMeshDesc).GetVertexSkinWeights();
		const FReferenceSkeleton& RefSkeleton = InSourceSkelMesh->GetRefSkeleton();

		const int32 TargetNumVertices = Context.IndexToVertexID.Num();

		// Each target vertex gets its own barycentric-interpolated weight map.
		TArray<TMap<FName, float>> PerVertexWeights;
		PerVertexWeights.SetNum(TargetNumVertices);

		ParallelFor(TEXT("MetaHuman Crowd Groom: Per-Vertex Weight Interpolation"), TargetNumVertices, 1, [&Context, &PerVertexWeights, &InParams, &SourceSkinWeights, &RefSkeleton](int32 CompactIndex)
		{
			const FVertexClosestInfo& Info = Context.VertexClosestInfo[CompactIndex];
			const FSourceTriangle& Tri = Context.SourceTriangles[Info.TriangleIndex];
			const FVector& ClosestBary = Info.BarycentricCoords;

			const TMap<FName, float> Weights0 = GetSourceVertexWeights(SourceSkinWeights, RefSkeleton, Context.ValidBoneIndices, Tri.V0);
			const TMap<FName, float> Weights1 = GetSourceVertexWeights(SourceSkinWeights, RefSkeleton, Context.ValidBoneIndices, Tri.V1);
			const TMap<FName, float> Weights2 = GetSourceVertexWeights(SourceSkinWeights, RefSkeleton, Context.ValidBoneIndices, Tri.V2);

			TSet<FName> AllBones;
			for (const TPair<FName, float>& Pair : Weights0)
			{
				AllBones.Add(Pair.Key);
			}
			for (const TPair<FName, float>& Pair : Weights1)
			{
				AllBones.Add(Pair.Key);
			}
			for (const TPair<FName, float>& Pair : Weights2)
			{
				AllBones.Add(Pair.Key);
			}

			TMap<FName, float>& InterpolatedWeights = PerVertexWeights[CompactIndex];
			for (FName BoneName : AllBones)
			{
				const float W0 = Weights0.Contains(BoneName) ? Weights0[BoneName] : 0.0f;
				const float W1 = Weights1.Contains(BoneName) ? Weights1[BoneName] : 0.0f;
				const float W2 = Weights2.Contains(BoneName) ? Weights2[BoneName] : 0.0f;

				const float InterpolatedWeight = W0 * ClosestBary.X + W1 * ClosestBary.Y + W2 * ClosestBary.Z;

				if (InterpolatedWeight > InParams.BlendWeightsThreshold)
				{
					InterpolatedWeights.Add(BoneName, InterpolatedWeight);
				}
			}
		});

		return WriteSkinWeightsToTarget(
			InOutTargetMeshDesc,
			Context.IndexToVertexID,
			[&PerVertexWeights](int32 CompactIndex) -> const TMap<FName, float>&
			{
				return PerVertexWeights[CompactIndex];
			});
	}
	/**
	 * Copies skin weights from source skeletal mesh directly into a target MeshDescription
	 * using the method specified in InParams. Does not call CommitMeshDescription, the
	 * caller is responsible for committing.
	 *
	 * @param InSourceSkelMesh		Source skeletal mesh to copy weights from
	 * @param InOutTargetMeshDesc	Target MeshDescription to write weights into
	 * @param InParams				Parameters controlling the weight transfer
	 * @return						True if successful, false otherwise
	 */
	bool CopySkinWeights(
		TNotNull<const USkeletalMesh*> InSourceSkelMesh,
		FMeshDescription& InOutTargetMeshDesc,
		const FGroomCardConversionParams& InParams,
		int32 InSourceLODIndex)
	{
		switch (InParams.SkinWeightMethod)
		{
		case EMetaHumanCrowdGroomSkinWeightCopyMethod::None:
			return true;
		case EMetaHumanCrowdGroomSkinWeightCopyMethod::SingleBone:
			return AssignAllVerticesToBone(InOutTargetMeshDesc, InParams.TargetBoneName);
		case EMetaHumanCrowdGroomSkinWeightCopyMethod::PerVertex:
			return CopySkinWeightsPerVertex(InSourceSkelMesh, InOutTargetMeshDesc, InParams, InSourceLODIndex);
		case EMetaHumanCrowdGroomSkinWeightCopyMethod::StrandBased:
			return CopySkinWeightsStrandBased(InSourceSkelMesh, InOutTargetMeshDesc, InParams, InSourceLODIndex);
		default:
			return false;
		}
	}

	/**
	 * Merges mesh descriptions from multiple static meshes into a single mesh description.
	 * Used to combine card meshes belonging to the same LOD level.
	 * Works directly with mesh descriptions to avoid building static mesh render data.
	 *
	 * @param InStaticMeshes Array of static meshes whose mesh descriptions will be merged
	 * @param OutLODMaterials Fresh per-LOD materials list (caller-owned, reset on entry).
	 *        One entry per polygon group in the merged mesh description, in the same order;
	 *        slot names are deduped within this LOD.
	 * @return Merged mesh description, or nullptr if merge fails
	 *
	 * @note Polygon-group dedupe: AppendMeshDescriptions merges polygon groups across source
	 *       meshes by ImportedMaterialSlotName. Two source meshes contributing the same slot
	 *       name collapse onto a single polygon group and a single OutLODMaterials entry.
	 */
	TUniquePtr<FMeshDescription> MergeMeshDescriptions(
		const TArray<UStaticMesh*>& InStaticMeshes,
		TArray<FSkeletalMaterial>& OutLODMaterials)
	{
		OutLODMaterials.Reset();

		if (InStaticMeshes.IsEmpty())
		{
			return nullptr;
		}

		// Collect source mesh descriptions, paired with the static mesh that contributed each
		// (so the materials walk below only sees meshes that contributed polygon groups).
		TArray<const FMeshDescription*> SourceMeshDescs;
		TArray<UStaticMesh*> ContributingStaticMeshes;
		for (UStaticMesh* StaticMesh : InStaticMeshes)
		{
			if (!StaticMesh)
			{
				continue;
			}
			if (const FMeshDescription* MeshDesc = StaticMesh->GetMeshDescription(0))
			{
				SourceMeshDescs.Add(MeshDesc);
				ContributingStaticMeshes.Add(StaticMesh);
			}
		}

		if (SourceMeshDescs.IsEmpty())
		{
			return nullptr;
		}

		// Dedup by ImportedMaterialSlotName to mirror AppendMeshDescriptions' polygon-group
		// merge rule, keeping OutLODMaterials.Num() aligned with the merged MD's PG count.
		TSet<FName> ExistingImportedSlotNames;
		for (UStaticMesh* StaticMesh : ContributingStaticMeshes)
		{
			for (const FStaticMaterial& StaticMaterial : StaticMesh->GetStaticMaterials())
			{
				if (ExistingImportedSlotNames.Contains(StaticMaterial.ImportedMaterialSlotName))
				{
					continue;
				}
				FSkeletalMaterial& SkelMaterial = OutLODMaterials.AddDefaulted_GetRef();
				SkelMaterial.MaterialInterface = StaticMaterial.MaterialInterface;
				SkelMaterial.MaterialSlotName = StaticMaterial.MaterialSlotName;
				SkelMaterial.ImportedMaterialSlotName = StaticMaterial.ImportedMaterialSlotName;
				SkelMaterial.UVChannelData = StaticMaterial.UVChannelData;

				ExistingImportedSlotNames.Add(StaticMaterial.ImportedMaterialSlotName);
			}
		}

		// Single mesh - clone its description directly
		if (SourceMeshDescs.Num() == 1)
		{
			return MakeUnique<FMeshDescription>(*SourceMeshDescs[0]);
		}

		// Merge multiple mesh descriptions
		TUniquePtr<FMeshDescription> MergedMeshDesc = MakeUnique<FMeshDescription>();

		// Determine max UV channels from source meshes before registering attributes
		int32 MaxUVChannels = 0;
		for (const FMeshDescription* SourceMeshDesc : SourceMeshDescs)
		{
			MaxUVChannels = FMath::Max(MaxUVChannels, SourceMeshDesc->GetNumUVElementChannels());
		}

		// Set UV channels before registering attributes
		if (MaxUVChannels > 0)
		{
			MergedMeshDesc->SetNumUVChannels(MaxUVChannels);
		}

		FStaticMeshAttributes(*MergedMeshDesc).Register();

		// Merge all source meshes into the target
		FStaticMeshOperations::FAppendSettings AppendSettings;
		AppendSettings.bMergeVertexColor = true;
		for (int32 ChannelIdx = 0; ChannelIdx < FStaticMeshOperations::FAppendSettings::MAX_NUM_UV_CHANNELS; ++ChannelIdx)
		{
			AppendSettings.bMergeUVChannels[ChannelIdx] = true;
		}

		FStaticMeshOperations::AppendMeshDescriptions(SourceMeshDescs, *MergedMeshDesc, AppendSettings);

		return MergedMeshDesc;
	}

	/**
	 * Prepares a mesh description for skeletal mesh conversion by adding bone and skin weight data.
	 * Registers skeletal mesh attributes, populates bone hierarchy from the reference skeleton,
	 * and assigns rigid skin weights (all vertices bound to root bone) as a starting point.
	 * The caller is expected to overwrite skin weights afterwards via CopySkinWeights.
	 *
	 * @param InOutMeshDesc Mesh description to prepare (modified in-place)
	 * @param InReferenceSkeleton Reference skeleton providing bone hierarchy
	 */
	void PrepareMeshDescriptionForSkeletalMesh(
		FMeshDescription& InOutMeshDesc,
		const FReferenceSkeleton& InReferenceSkeleton)
	{
		FSkeletalMeshAttributes SkeletalMeshAttributes(InOutMeshDesc);
		SkeletalMeshAttributes.Register();

		// Fill bone data from reference skeleton
		const int32 NumRefBones = InReferenceSkeleton.GetRawBoneNum();

		FSkeletalMeshAttributes::FBoneArray& Bones = SkeletalMeshAttributes.Bones();
		Bones.Reset(NumRefBones);

		FSkeletalMeshAttributes::FBoneNameAttributesRef BoneNames = SkeletalMeshAttributes.GetBoneNames();
		FSkeletalMeshAttributes::FBoneParentIndexAttributesRef BoneParentIndices = SkeletalMeshAttributes.GetBoneParentIndices();
		FSkeletalMeshAttributes::FBonePoseAttributesRef BonePoses = SkeletalMeshAttributes.GetBonePoses();

		for (int32 Index = 0; Index < NumRefBones; ++Index)
		{
			const FMeshBoneInfo& BoneInfo = InReferenceSkeleton.GetRawRefBoneInfo()[Index];
			const FTransform& BoneTransform = InReferenceSkeleton.GetRawRefBonePose()[Index];

			const FBoneID BoneID = SkeletalMeshAttributes.CreateBone();

			BoneNames.Set(BoneID, BoneInfo.Name);
			BoneParentIndices.Set(BoneID, BoneInfo.ParentIndex);
			BonePoses.Set(BoneID, BoneTransform);
		}

		// Assign rigid binding to root bone (will be overwritten by CopySkinWeights)
		FSkinWeightsVertexAttributesRef SkinWeights = SkeletalMeshAttributes.GetVertexSkinWeights();
		UE::AnimationCore::FBoneWeight RootInfluence(0, 1.0f);
		UE::AnimationCore::FBoneWeights RootBinding = UE::AnimationCore::FBoneWeights::Create({RootInfluence});

		for (const FVertexID VertexID : InOutMeshDesc.Vertices().GetElementIDs())
		{
			SkinWeights.Set(VertexID, RootBinding);
		}
	}

	/**
	 * Prepares merged groom mesh descriptions (cards and/or helmets) as a geometry bundle
	 * ready for the crowd pipeline.
	 * Writes bone hierarchy and real skin weights (transferred from the source skeletal mesh)
	 * directly into each MD, then packages them alongside the source RefSkeleton and the given
	 * Materials into an FMetaHumanCrowdMeshGeometryBundle. Empty mesh descriptions are skipped,
	 * allowing sparse LOD levels.
	 *
	 * @param InMeshDescriptions Array of mesh descriptions, one per LOD (will be consumed)
	 * @param InMaterials Materials to attach to the bundle
	 * @param InLODMaterialMaps InLODMaterialMaps Per-LOD section->material remap, one entry per InMeshDescriptions
	 *        entry. Each entry maps polygon group IDs in the LOD's MD to indices into
	 *        InMaterials. Empty entries fall back to identity at resolve time.
	 * @param InSourceSkelMesh Source skeletal mesh providing the RefSkeleton and skin weights
	 * @param InPerLODParams Per-LOD weight-transfer parameters (one entry per InMeshDescriptions
	 *        entry, indexed by the same position). Allows different LODs to use different
	 *        skin-weight methods (e.g. SingleBone for helmet caps, StrandBased for cards).
	 * @param OutBundle Bundle to populate on success
	 * @return True if preparation succeeded, false otherwise
	 */
	bool TryPrepareGroomGeometryBundle(
		TArray<FMeshDescription>&& InMeshDescriptions,
		TArray<FSkeletalMaterial>&& InMaterials,
		TArray<TArray<int32>>&& InLODMaterialMaps,
		TNotNull<const USkeletalMesh*> InSourceSkelMesh,
		TConstArrayView<FGroomCardConversionParams> InPerLODParams,
		FMetaHumanCrowdMeshGeometryBundle& OutBundle)
	{
		if (!InSourceSkelMesh->GetSkeleton())
		{
			return false;
		}

		check(InPerLODParams.Num() == InMeshDescriptions.Num());
		check(InLODMaterialMaps.Num() == InMeshDescriptions.Num());

		// Prepare each mesh description in place. Empty slots are kept as-is so the array
		// stays sparse-by-LOD-index: the collection pipeline indexes BundleMeshDescriptions
		// by groom LOD, and helmets typically only populate high LODs (5+) with low LODs
		// empty - collapsing those out would shift helmets into LOD 0 and break the
		// "groom LOD == face LOD" alignment that BuildCollection relies on.
		bool bAnyPrepared = false;
		for (int32 LODIndex = 0; LODIndex < InMeshDescriptions.Num(); ++LODIndex)
		{
			FMeshDescription& MeshDesc = InMeshDescriptions[LODIndex];
			if (MeshDesc.Vertices().Num() == 0)
			{
				continue;
			}

			PrepareMeshDescriptionForSkeletalMesh(MeshDesc, InSourceSkelMesh->GetRefSkeleton());

			if (!CopySkinWeights(InSourceSkelMesh, MeshDesc, InPerLODParams[LODIndex], LODIndex))
			{
				UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "TryPrepareGroomGeometryBundle: failed to transfer skin weights at LOD {LOD}", LODIndex);
				return false;
			}

			bAnyPrepared = true;
		}

		if (!bAnyPrepared)
		{
			return false;
		}

		OutBundle.MeshDescriptions = MoveTemp(InMeshDescriptions);
		OutBundle.RefSkeleton = InSourceSkelMesh->GetRefSkeleton();
		OutBundle.Materials = MoveTemp(InMaterials);
		OutBundle.LODMaterialMaps = MoveTemp(InLODMaterialMaps);

		return true;
	}

	/**
	 * Per-LOD entry describing a single groom mesh: either a hair card or a helmet.
	 * Both FHairGroupsCardsSourceDescription and FHairGroupsMeshesSourceDescription
	 * boil down to (Mesh, LODIndex, Textures), so we collapse them into one shape
	 * for the rest of the pipeline.
	 */
	struct FGroomMeshInfo
	{
		UStaticMesh* Mesh = nullptr;
		int32 LODIndex = 0;
		FHairGroupCardsTextures Textures;
		bool bIsHelmet = false;
	};

	/**
	 * Collects groom meshes from a UGroomAsset, falling back from cards to helmets per-LOD.
	 *
	 * For each LODIndex present across cards/helmets, cards take priority: if any card
	 * description has a mesh at that LOD, all card descriptions contributing meshes at that
	 * LOD are taken and helmets at that LOD are ignored. Otherwise the helmet entries at that
	 * LOD are used. This lets a groom ship cards for low LODs and a helmet for distant ones,
	 * or use only cards / only helmets across the board.
	 *
	 * @param InGroomAsset Source groom to read from.
	 * @return Flat list of groom meshes ready for downstream LOD merging.
	 */
	TArray<FGroomMeshInfo> CollectGroomMeshes(const TNotNull<UGroomAsset*> InGroomAsset)
	{
		TArray<FGroomMeshInfo> Result;

		const TArray<FHairGroupsCardsSourceDescription>& CardsArray = InGroomAsset->GetHairGroupsCards();
		const TArray<FHairGroupsMeshesSourceDescription>& HelmetsArray = InGroomAsset->GetHairGroupsMeshes();

		// First pass: figure out which LODs have at least one card mesh. Those LODs are
		// "owned" by cards; helmets at those LODs get skipped.
		TSet<int32> LODsCoveredByCards;
		for (const FHairGroupsCardsSourceDescription& CardDesc : CardsArray)
		{
			if (CardDesc.GetMesh())
			{
				LODsCoveredByCards.Add(CardDesc.LODIndex);
			}
		}

		for (const FHairGroupsCardsSourceDescription& CardDesc : CardsArray)
		{
			if (UStaticMesh* Mesh = CardDesc.GetMesh())
			{
				FGroomMeshInfo& Info = Result.AddDefaulted_GetRef();
				Info.Mesh = Mesh;
				Info.LODIndex = CardDesc.LODIndex;
				Info.Textures = CardDesc.Textures;
			}
		}

		for (const FHairGroupsMeshesSourceDescription& HelmetDesc : HelmetsArray)
		{
			if (UStaticMesh* Mesh = HelmetDesc.ImportedMesh)
			{
				if (LODsCoveredByCards.Contains(HelmetDesc.LODIndex))
				{
					// Cards already cover this LOD; skip the helmet.
					continue;
				}

				FGroomMeshInfo& Info = Result.AddDefaulted_GetRef();
				Info.Mesh = Mesh;
				Info.LODIndex = HelmetDesc.LODIndex;
				Info.Textures = HelmetDesc.Textures;
				Info.bIsHelmet = true;
			}
		}

		return Result;
	}

	/**
	 * Creates material instances for groom (hair card or helmet) materials.
	 * Applies groom textures to the parent material's parameters and replaces
	 * the MaterialInterface on each entry in the materials array.
	 *
	 * @param InCardsParent Parent material for cards LODs.
	 * @param InHelmetsParent Parent material for helmet LODs. May be null when no helmet
	 *        LODs are present in this groom; if a helmet LOD is reached without a parent
	 *        configured, this function logs and falls back to InCardsParent.
	 * @param InTextures Contains the groom textures used for the material.
	 * @param bIsHelmet Selects which parent to use.
	 * @param InOuter Outer object for the created material instances.
	 * @param InMICBaseName Base name for the new MICs. Single-entry InOutMaterials uses this name
	 *        directly; multi-entry uses InMICBaseName + "_" + slot name to disambiguate.
	 * @param InOutMaterials Materials array to update in-place.
	 *
	 * @note Currently only supports compact layouts (Layout2, Layout3).
	 *       Layout0/Layout1 with separate textures are not supported.
	 */
	void CreateGroomMaterialInstances(
		TNotNull<UMaterialInterface*> InCardsParent,
		UMaterialInterface* InHelmetsParent,
		const FHairGroupCardsTextures& InTextures,
		bool bIsHelmet,
		UObject* InOuter,
		const FString& InMICBaseName,
		TArray<FSkeletalMaterial>& InOutMaterials)
	{
		// Compact layouts require at least 2 textures
		if (InTextures.Textures.Num() < 2)
		{
			return;
		}

		UMaterialInterface* ParentMaterial = InCardsParent;
		if (bIsHelmet)
		{
			if (InHelmetsParent)
			{
				ParentMaterial = InHelmetsParent;
			}
			else
			{
				UE_LOGFMT(LogMetaHumanCrowdEditor, Warning,
					"CreateGroomMaterialInstances: helmet LOD encountered but no HelmetsMaterial set on the groom pipeline -- falling back to CardsMaterial. Helmet rendering will be incorrect.");
			}
		}

		// For compact layouts:
		// Index 0: TangentCoordU
		// Index 1: CoverageDepthSeed (Layout2) or ColorXYDepthGroupID (Layout3)
		UTexture2D* TangentCoordUTexture = InTextures.Textures[0];
		UTexture2D* SecondaryTexture = InTextures.Textures[1];

		const bool bMultipleMaterials = InOutMaterials.Num() > 1;
		for (FSkeletalMaterial& Material : InOutMaterials)
		{
			const FString DesiredName = bMultipleMaterials
				? FString::Format(TEXT("{0}_{1}"), { InMICBaseName, Material.MaterialSlotName.ToString() })
				: InMICBaseName;
			UMaterialInstanceConstant* MaterialInstance = NewObject<UMaterialInstanceConstant>(InOuter, FName(*DesiredName), RF_Public);
			if (MaterialInstance)
			{
				MaterialInstance->SetParentEditorOnly(ParentMaterial);
				MaterialInstance->SetTextureParameterValueEditorOnly(FName("Tangent-CoordU"), TangentCoordUTexture);
				MaterialInstance->SetTextureParameterValueEditorOnly(FName("Coverage-Depth-Seed"), SecondaryTexture);
			}

			Material.MaterialInterface = MaterialInstance;
		}
	}

	/**
	 * Stable LOD-indexed slot name ("LOD{N}_{Card|Helmet}", or "LOD{N}_{M}_{Kind}" for
	 * multi-material LODs). Decouples runtime authoring from upstream static-mesh slot names,
	 * which are author-driven and unstable.
	 */
	static FName MakeCrowdGroomLODSlotName(bool bIsHelmet, int32 InOutputLODIndex, int32 InMaterialIndexWithinLOD)
	{
		const TCHAR* KindSuffix = bIsHelmet
			? TEXT("Helmet")
			: TEXT("Card");

		if (InMaterialIndexWithinLOD == 0)
		{
			return FName(*FString::Format(TEXT("LOD{0}_{1}"), { InOutputLODIndex, KindSuffix }));
		}
		else
		{
			return FName(*FString::Format(TEXT("LOD{0}_{1}_{2}"), { InOutputLODIndex, InMaterialIndexWithinLOD, KindSuffix }));
		}
	}

	/**
	 * Per-LOD material assembly: merges this LOD's groom static-mesh slots into a fresh
	 * FSkeletalMaterial list, configures one MIC per slot parented to either the cards or
	 * helmets material (per FGroomMeshInfo::bIsHelmet), then appends onto the bundle-wide
	 * accumulators and records the offsets the caller needs for LODMaterialMaps.
	 *
	 * Each LOD owns its own slice of InOutMergedMaterials so cards and helmet LODs can carry
	 * distinct MICs even when their static meshes share slot names. The bundle's
	 * LODMaterialMaps[LOD] then remaps that LOD's polygon groups into the bundle-wide
	 * Materials array.
	 *
	 * @param InLODGroomMeshes The FGroomMeshInfo entries that contributed geometry to this
	 *        LOD. Their static meshes provide the slot names; the first non-null entry's
	 *        Textures and bIsHelmet drive MIC configuration. Cards-or-helmet partition is
	 *        uniform per LOD (see CollectGroomMeshes).
	 * @param InCardsParent Parent material for cards LODs.
	 * @param InHelmetsParent Parent material for helmet LODs (may be null).
	 * @param InOuter Outer for the created MICs.
	 * @param InMICBaseName Base name for the MICs this LOD creates. Forwarded to
	 *        CreateGroomMaterialInstances which decides whether to use it as-is or append a
	 *        slot-name suffix when the LOD has multiple slots.
	 * @param InOutputLODIndex The position of this LOD within the consuming variant's LOD list
	 *        (i.e. the output LOD index on the assembled SKM). Used to name the produced
	 *        material slots with the stable LOD-indexed convention; see MakeCrowdGroomLODSlotName.
	 * @param InOutMergedMaterials Bundle-wide materials array; appended to.
	 * @param OutLODMaterialMap Identity-shifted-by-offset PG -> Material map for this LOD,
	 *        sized to the LOD's local material count, written to InOutMergedMaterials offsets.
	 *        Empty if the LOD has no static meshes (caller treats as identity).
	 * @return True on success, false if the LOD had no usable static meshes (caller should
	 *         treat the LOD as material-less and leave OutLODMaterialMap empty).
	 */
	bool BuildLODMaterials(
		TConstArrayView<FGroomMeshInfo> InLODGroomMeshes,
		TNotNull<UMaterialInterface*> InCardsParent,
		UMaterialInterface* InHelmetsParent,
		UObject* InOuter,
		const FString& InMICBaseName,
		int32 InOutputLODIndex,
		TArray<FSkeletalMaterial>& InOutMergedMaterials,
		TArray<int32>& OutLODMaterialMap)
	{
		OutLODMaterialMap.Reset();

		// Collect this LOD's non-null static meshes plus pick the first usable FGroomMeshInfo
		// for the textures + bIsHelmet flag. The cards-or-helmet partition is uniform per LOD
		// (see CollectGroomMeshes), so any non-null entry is representative.
		TArray<UStaticMesh*> LODStaticMeshes;
		const FGroomMeshInfo* DrivingMeshInfo = nullptr;
		for (const FGroomMeshInfo& MeshInfo : InLODGroomMeshes)
		{
			if (MeshInfo.Mesh)
			{
				LODStaticMeshes.Add(MeshInfo.Mesh);
				if (!DrivingMeshInfo)
				{
					DrivingMeshInfo = &MeshInfo;
				}
			}
		}

		if (!DrivingMeshInfo)
		{
			return false;
		}

		// Build LODLocalMaterials so PG ID i in the merged MD maps to LODLocalMaterials[i].
		// Dedup by ImportedMaterialSlotName to mirror AppendMeshDescriptions' polygon-group
		// merge rule, keying off MaterialSlotName instead would desync the counts.
		TArray<FSkeletalMaterial> LODLocalMaterials;
		{
			TSet<FName> ExistingImportedSlotNames;
			for (UStaticMesh* StaticMesh : LODStaticMeshes)
			{
				if (!StaticMesh)
				{
					continue;
				}
				for (const FStaticMaterial& StaticMaterial : StaticMesh->GetStaticMaterials())
				{
					if (ExistingImportedSlotNames.Contains(StaticMaterial.ImportedMaterialSlotName))
					{
						continue;
					}

					const FName DisplaySlotName = MakeCrowdGroomLODSlotName(DrivingMeshInfo->bIsHelmet, InOutputLODIndex, LODLocalMaterials.Num());

					FSkeletalMaterial& SkelMaterial = LODLocalMaterials.AddDefaulted_GetRef();
					SkelMaterial.MaterialInterface = StaticMaterial.MaterialInterface;
					SkelMaterial.MaterialSlotName = DisplaySlotName;
					SkelMaterial.ImportedMaterialSlotName = StaticMaterial.ImportedMaterialSlotName;
					SkelMaterial.UVChannelData = StaticMaterial.UVChannelData;

					ExistingImportedSlotNames.Add(StaticMaterial.ImportedMaterialSlotName);
				}
			}
		}

		if (LODLocalMaterials.IsEmpty())
		{
			return false;
		}

		// Configure MICs in-place on the LOD-local materials list. CreateGroomMaterialInstances
		// picks the parent material based on bIsHelmet (CardsMaterial vs HelmetsMaterial).
		CreateGroomMaterialInstances(
			InCardsParent,
			InHelmetsParent,
			DrivingMeshInfo->Textures,
			DrivingMeshInfo->bIsHelmet,
			InOuter,
			InMICBaseName,
			LODLocalMaterials);

		// Append onto the bundle-wide list and emit an identity-shifted-by-offset map.
		const int32 Offset = InOutMergedMaterials.Num();
		OutLODMaterialMap.Reserve(LODLocalMaterials.Num());
		for (int32 LocalIdx = 0; LocalIdx < LODLocalMaterials.Num(); ++LocalIdx)
		{
			OutLODMaterialMap.Add(Offset + LocalIdx);
		}
		InOutMergedMaterials.Append(MoveTemp(LODLocalMaterials));

		return true;
	}

	/**
	 * Populates OutEntry with a baked-groom texture binding for a (groom, head) pair when the
	 * groom pipeline has BakedGroomTexture configured. The baked texture binds at every face LOD
	 * >= InGroomTextureMinLOD that the build will consume.
	 *
	 * Returns false (and leaves OutEntry untouched) if BakedGroomTexture is unset or no face
	 * LODs satisfy the threshold.
	 */
	bool BuildBakedGroomTextureEntry(
		UTexture* InBakedGroomTexture,
		FName InPipelineSlotName,
		int32 InGroomTextureMinLOD,
		TConstArrayView<int32> InActorFaceLODs,
		TConstArrayView<int32> InInstancedFaceLODs,
		FMetaHumanCrowdGroomBakedTextureEntry& OutEntry)
	{
		if (!InBakedGroomTexture)
		{
			return false;
		}

		// Collect every face LOD the build will consume, dedup, and filter to LODs >= threshold.
		TSet<int32> ConsumedFaceLODs;
		ConsumedFaceLODs.Reserve(InActorFaceLODs.Num() + InInstancedFaceLODs.Num());
		for (const int32 LOD : InActorFaceLODs)
		{
			ConsumedFaceLODs.Add(LOD);
		}
		for (const int32 LOD : InInstancedFaceLODs)
		{
			ConsumedFaceLODs.Add(LOD);
		}

		TArray<int32> AppliesAtFaceLODs;
		for (const int32 FaceLOD : ConsumedFaceLODs)
		{
			if (FaceLOD >= InGroomTextureMinLOD)
			{
				AppliesAtFaceLODs.Add(FaceLOD);
			}
		}

		if (AppliesAtFaceLODs.IsEmpty())
		{
			return false;
		}

		// Stable order eases downstream debugging.
		AppliesAtFaceLODs.Sort();

		OutEntry.Texture = InBakedGroomTexture;
		OutEntry.PipelineSlotName = InPipelineSlotName;
		OutEntry.AppliesAtFaceLODs = MoveTemp(AppliesAtFaceLODs);
		OutEntry.GroomTextureMinLOD = InGroomTextureMinLOD;
		return true;
	}

	/**
	 * Builds one variant's (actor or instanced) MIC list and per-LOD section->material remap
	 * for a single (groom, head) pair. Walks the variant's face-LOD list in order; for each
	 * source groom-LOD that has FGroomMeshInfo content, calls BuildLODMaterials to append a
	 * fresh slice of MICs onto OutMaterials and record an offset map at OutLODMaterialMaps
	 * indexed by the source groom-LOD.
	 *
	 * Naming: each MIC produced gets a base name "MI_{Groom}_{Head}_{Card|Helmet}_LOD{N}_{Variant}"
	 * where N is the consuming-mesh output-LOD index (the position in InVariantFaceLODs).
	 *
	 * @param InLODGroomMeshInfosByLOD Sparse array indexed by source groom-LOD; each populated
	 *        slot holds the FGroomMeshInfos that contributed geometry to that LOD.
	 * @param InVariantFaceLODs The consuming mesh's LOD list (e.g. ActorFaceLODs). Acts as both
	 *        the iteration order and the source-of-truth for output-LOD position mapping.
	 * @param InCardsParent Parent material for cards LODs.
	 * @param InHelmetsParent Parent material for helmet LODs (may be null).
	 * @param InOuter Outer for the new MICs.
	 * @param InGroomAssetName Used in the MIC asset name.
	 * @param InHeadAssetName Used in the MIC asset name.
	 * @param InVariantSuffix "Actor" or "Inst" - tail of the MIC name.
	 * @param OutMaterials Reset and populated with the variant's MICs (caller-owned).
	 * @param OutLODMaterialMaps Reset, sized to InLODGroomMeshInfosByLOD.Num(), and populated for
	 *        each consumed source groom-LOD with a PG -> OutMaterials index map.
	 */
	void BuildVariantMaterials(
		TConstArrayView<TArray<FGroomMeshInfo>> InLODGroomMeshInfosByLOD,
		TConstArrayView<int32> InVariantFaceLODs,
		TNotNull<UMaterialInterface*> InCardsParent,
		UMaterialInterface* InHelmetsParent,
		UObject* InOuter,
		const FString& InGroomAssetName,
		const FString& InHeadAssetName,
		const FString& InVariantSuffix,
		const TMap<FName, FMetaHumanCrowdOutfitInstancedMaterial>* InPerSlotParentOverrides,
		TArray<FSkeletalMaterial>& OutMaterials,
		TArray<TArray<int32>>& OutLODMaterialMaps)
	{
		OutMaterials.Reset();
		OutLODMaterialMaps.Reset();
		OutLODMaterialMaps.SetNum(InLODGroomMeshInfosByLOD.Num());

		for (int32 OutputLODIndex = 0; OutputLODIndex < InVariantFaceLODs.Num(); ++OutputLODIndex)
		{
			const int32 GroomLODIndex = InVariantFaceLODs[OutputLODIndex];
			if (!InLODGroomMeshInfosByLOD.IsValidIndex(GroomLODIndex))
			{
				continue;
			}
			const TArray<FGroomMeshInfo>& LODGroomMeshInfos = InLODGroomMeshInfosByLOD[GroomLODIndex];
			if (LODGroomMeshInfos.IsEmpty())
			{
				continue;
			}

			const bool bIsHelmet = LODGroomMeshInfos[0].bIsHelmet;
			const FString& GroomType = bIsHelmet ? TEXT("Helmet") : TEXT("Card");
			const FString MICBaseName = FString::Format(
				TEXT("MI_{0}_{1}_{2}_LOD{3}_{4}"),
				{ InGroomAssetName, InHeadAssetName, GroomType, OutputLODIndex, InVariantSuffix });

			UMaterialInterface* ResolvedCardsParent = InCardsParent;
			UMaterialInterface* ResolvedHelmetsParent = InHelmetsParent;

			if (InPerSlotParentOverrides)
			{
				const FName FirstSlotName = MakeCrowdGroomLODSlotName(bIsHelmet, OutputLODIndex, 0);
				if (const FMetaHumanCrowdOutfitInstancedMaterial* OverrideEntry = InPerSlotParentOverrides->Find(FirstSlotName))
				{
					if (OverrideEntry->InstancedComponentMaterial)
					{
						if (bIsHelmet)
						{
							ResolvedHelmetsParent = OverrideEntry->InstancedComponentMaterial;
						}
						else
						{
							ResolvedCardsParent = OverrideEntry->InstancedComponentMaterial;
						}
					}
				}
			}

			BuildLODMaterials(
				LODGroomMeshInfos,
				ResolvedCardsParent,
				ResolvedHelmetsParent,
				InOuter,
				MICBaseName,
				OutputLODIndex,
				OutMaterials,
				OutLODMaterialMaps[GroomLODIndex]);
		}
	}

}

UMetaHumanCrowdGroomEditorPipeline::UMetaHumanCrowdGroomEditorPipeline()
{
	Specification = CreateDefaultSubobject<UMetaHumanCharacterEditorPipelineSpecification>("Specification");
	Specification->BuildInputStruct = FMetaHumanCrowdGroomBuildInput::StaticStruct();

	CardConversionParams.TargetBoneName = FName("head");
	CardConversionParams.ExcludedMaterialSlotNames = { FName("eyelashes_shader_shader"), FName("eyelashes_HiLOD_shader_shader")};
}

UE::Tasks::TTask<FMetaHumanPaletteBuiltData> UMetaHumanCrowdGroomEditorPipeline::BuildItem(const FBuildItemParams& Params) const
{
	const UMetaHumanCrowdGroomPipeline* RuntimePipeline = Cast<UMetaHumanCrowdGroomPipeline>(GetRuntimePipeline());

	if (!RuntimePipeline)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "Runtime pipeline for item {ItemPath} must be a UMetaHumanCrowdGroomPipeline", Params.ItemPath.ToDebugString());

		return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
	}

	if (!Params.BuildInput.GetPtr<FMetaHumanCrowdGroomBuildInput>())
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "Build input not provided to Crowd Groom pipeline during build");

		return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
	}

	const UObject* LoadedAsset = Params.WardrobeItem->PrincipalAsset.LoadSynchronous();
	const UGroomBindingAsset* GroomBinding = Cast<UGroomBindingAsset>(LoadedAsset);
	if (!GroomBinding)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "Crowd Groom pipeline failed to load Groom {Groom} during build", Params.WardrobeItem->PrincipalAsset.ToString());

		return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
	}

	UGroomAsset* SourceGroom = GroomBinding->GetGroom();
	if (!SourceGroom)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "Crowd Groom pipeline failed to load Groom {Groom} during build, missing groom asset", Params.WardrobeItem->PrincipalAsset.ToString());

		return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
	}

	UMaterialInterface* LoadedCardsMaterial = CardsMaterial.LoadSynchronous();
	if (!LoadedCardsMaterial)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "Crowd Groom pipeline failed to load cards material {CardsMaterial} during build", CardsMaterial.ToString());

		return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
	}

	UMaterialInterface* LoadedHelmetsMaterial = HelmetsMaterial.LoadSynchronous();
	if (!LoadedHelmetsMaterial)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "Crowd Groom pipeline failed to load helmets material {HelmetsMaterial} during build", HelmetsMaterial.ToString());

		return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
	}

	const FMetaHumanCrowdGroomBuildInput& GroomBuildInput = Params.BuildInput.Get<FMetaHumanCrowdGroomBuildInput>();

	FMetaHumanPaletteBuiltData BuiltDataResult;
	FMetaHumanPipelineBuiltData& GroomBuiltData = BuiltDataResult.ItemBuiltData.Edit().Add(Params.ItemPath);
	GroomBuiltData.DefaultUnpackSubfolder = FString::Format(TEXT("Grooms/{0}"), { LoadedAsset->GetName() });

	FMetaHumanCrowdGroomBuildOutput& GroomBuildOutput = GroomBuiltData.BuildOutput.InitializeAs<FMetaHumanCrowdGroomBuildOutput>();
	GroomBuildOutput.GroomAssetName = LoadedAsset->GetName();
	GroomBuildOutput.PipelineSlotName = GroomBuildInput.PipelineSlotName;

	// Resolve the effective baked-groom configuration. Authoring on the crowd editor pipeline wins.
	// When BakedGroomTexture is unset or GroomTextureMinLOD is -1, fall back to the matching field
	// on the source groom's default pipeline (RuntimePipeline->SourceGroomItem).
	TSoftObjectPtr<UTexture> EffectiveBakedGroomTexture = BakedGroomTexture;
	int32 EffectiveGroomTextureMinLOD = GroomTextureMinLOD;

	if (RuntimePipeline->SourceGroomItem)
	{
		if (const UMetaHumanDefaultGroomPipeline* DefaultGroomPipeline =
				Cast<UMetaHumanDefaultGroomPipeline>(RuntimePipeline->SourceGroomItem->GetPipeline()))
		{
			if (EffectiveBakedGroomTexture.IsNull())
			{
				EffectiveBakedGroomTexture = DefaultGroomPipeline->BakedGroomTexture;
			}
			if (EffectiveGroomTextureMinLOD < 0)
			{
				EffectiveGroomTextureMinLOD = DefaultGroomPipeline->GroomTextureMinLOD;
			}
		}
	}
	EffectiveGroomTextureMinLOD = FMath::Max(0, EffectiveGroomTextureMinLOD);

	UTexture* LoadedBakedGroomTexture = EffectiveBakedGroomTexture.LoadSynchronous();

	const bool bSourceGroomHasGeometry =
		!SourceGroom->GetHairGroupsCards().IsEmpty() ||
		!SourceGroom->GetHairGroupsMeshes().IsEmpty();

	// Bail out if the source groom has nothing to fit
	if (!bSourceGroomHasGeometry && !LoadedBakedGroomTexture)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Display,
			"Crowd Groom pipeline: source groom {Groom} has no card or helmet meshes, nor baked textures; skipping all fit targets.",
			SourceGroom->GetPathName());
		return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>(MoveTemp(BuiltDataResult));
	}

	const ITargetPlatform* TargetPlatform = GetTargetPlatformManager()->GetRunningTargetPlatform();

	using namespace UE::MetaHuman::Private;

	struct FGroomFitJob
	{
		FMetaHumanPaletteItemKey TargetItemKey;
		USkeletalMesh* TargetMesh = nullptr;
		UGroomBindingAsset* NewGroomBinding = nullptr;
		UGroomAsset* NewGroom = nullptr;
		TArray<FGroomMeshInfo> GroomMeshes;
	};

	TArray<FGroomFitJob> GroomFitJobs;

	for (const TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdGroomFitTarget>& FitPair : GroomBuildInput.FitTargets)
	{
		if (!FitPair.Value.OptionalCharacter.IsNull() 
			&& CompatibleHeads.Num() > 0 
			&& !CompatibleHeads.Contains(FitPair.Value.OptionalCharacter))
		{
			continue;
		}

		// Skip RBF/DDC work as there is no source geometry - only the baked-texture fallback applies.
		if (!bSourceGroomHasGeometry)
		{
			FMetaHumanCrowdGroomBakedTextureEntry BakedEntry;
			if (BuildBakedGroomTextureEntry(
					LoadedBakedGroomTexture,
					GroomBuildInput.PipelineSlotName,
					EffectiveGroomTextureMinLOD,
					GroomBuildInput.ActorFaceLODs,
					GroomBuildInput.InstancedFaceLODs,
					BakedEntry))
			{
				GroomBuildOutput.ItemToBakedGroomTexture.Add(FitPair.Key, MoveTemp(BakedEntry));
			}

			// Strands-only grooms have no card/helmet geometry, so the normal
			// GenerateAssemblyParameters call (which queries the groom's own card materials)
			// is unreachable. Feed it the cards material as a synthetic slot so the
			// UI-facing property bag gets populated and the user sees colour sliders.
			if (LoadedCardsMaterial)
			{
				const auto FetchSlotName = UE::MetaHuman::MaterialUtils::FFetchSlotNameDelegate::CreateLambda(
					[](int32) { return FName(); });
				const auto FetchSlotMaterial = UE::MetaHuman::MaterialUtils::FFetchSlotMaterialDelegate::CreateLambda(
					[LoadedCardsMaterial](int32) -> const UMaterialInterface* { return LoadedCardsMaterial; });

				UE::MetaHuman::MaterialUtils::GenerateAssemblyParameters(
					{},
					RuntimePipeline->RuntimeMaterialParameters,
					/*NumMaterialSlots=*/ 1,
					FetchSlotName,
					FetchSlotMaterial,
					GroomBuiltData.AssemblyParameters);
			}

			continue;
		}

		const FString TargetMeshDerivedDataKey = FitPair.Value.TargetMesh->GetDerivedDataKey();
		const UE::DerivedData::FCacheKey CacheKey = MakeGroomFittingCacheKey(
			TargetMeshDerivedDataKey, GroomBinding, SourceGroom, CardConversionParams, LoadedCardsMaterial, Params.BuildCacheGuid);

		{
			FMetaHumanCrowdMeshGeometryBundle GroomBundle;

			if (TryLoadGroomBundleFromDDC(CacheKey, FitPair.Value.TargetMesh, GroomBundle))
			{
				// Reconstitute materials and LODMaterialMaps from the source groom. The DDC
				// only caches geometry (MeshDescriptions); the per-variant MIC config has to be
				// rebuilt from the source UGroomAsset, which is deterministic over the asset
				// (CollectGroomMeshes returns the same partition pre- and post-RBF).
				const TArray<FGroomMeshInfo> SourceGroomMeshes = CollectGroomMeshes(SourceGroom);

				// Pack into sparse-by-groom-LOD-index shape so BuildVariantMaterials can iterate
				// the variant's face-LOD list and look up by LOD index. Sized to the cached MD
				// count so the resulting LODMaterialMaps line up with Bundle.MeshDescriptions.
				TArray<TArray<FGroomMeshInfo>> LODGroomMeshInfosByLOD;
				LODGroomMeshInfosByLOD.SetNum(GroomBundle.MeshDescriptions.Num());
				for (const FGroomMeshInfo& MeshInfo : SourceGroomMeshes)
				{
					if (MeshInfo.Mesh && LODGroomMeshInfosByLOD.IsValidIndex(MeshInfo.LODIndex))
					{
						LODGroomMeshInfosByLOD[MeshInfo.LODIndex].Add(MeshInfo);
					}
				}

				const FString HeadAssetName = FitPair.Key.ToAssetNameString();

				BuildVariantMaterials(
					LODGroomMeshInfosByLOD,
					GroomBuildInput.ActorFaceLODs,
					LoadedCardsMaterial,
					LoadedHelmetsMaterial,
					Params.OuterForGeneratedObjects,
					GroomBuildOutput.GroomAssetName,
					HeadAssetName,
					TEXT("Actor"),
					nullptr,
					GroomBundle.Materials,
					GroomBundle.LODMaterialMaps);

				// Instanced variant: only Materials/LODMaterialMaps differ from the actor bundle
				FMetaHumanCrowdGroomMaterialOverride InstancedOverride;
				BuildVariantMaterials(
					LODGroomMeshInfosByLOD,
					GroomBuildInput.InstancedFaceLODs,
					LoadedCardsMaterial,
					LoadedHelmetsMaterial,
					Params.OuterForGeneratedObjects,
					GroomBuildOutput.GroomAssetName,
					HeadAssetName,
					TEXT("Inst"),
					&RuntimePipeline->InstancedComponentOverrideMaterials,
					InstancedOverride.Materials,
					InstancedOverride.LODMaterialMaps);

				const TArray<FSkeletalMaterial>& BundleMaterials = GroomBuildOutput.ItemToGroomGeometryMap.Add(FitPair.Key, MoveTemp(GroomBundle)).Materials;
				GroomBuildOutput.InstancedMaterialOverrides.Add(FitPair.Key, MoveTemp(InstancedOverride));

				FMetaHumanCrowdGroomBakedTextureEntry BakedEntry;
				if (BuildBakedGroomTextureEntry(
						LoadedBakedGroomTexture,
						GroomBuildInput.PipelineSlotName,
						EffectiveGroomTextureMinLOD,
						GroomBuildInput.ActorFaceLODs,
						GroomBuildInput.InstancedFaceLODs,
						BakedEntry))
				{
					GroomBuildOutput.ItemToBakedGroomTexture.Add(FitPair.Key, MoveTemp(BakedEntry));
				}

				UE::MetaHuman::MaterialUtils::GenerateAssemblyParameters(
					{},
					RuntimePipeline->RuntimeMaterialParameters,
					BundleMaterials.Num(),
					UE::MetaHuman::MaterialUtils::MakeFetchSlotNameDelegate(BundleMaterials),
					UE::MetaHuman::MaterialUtils::MakeFetchSlotMaterialDelegate(BundleMaterials),
					GroomBuiltData.AssemblyParameters);

				// The fitted groom was fetched from the DDC, so this iteration is done.
				continue;
			}
		}

		// DDC miss: set up for RBF fitting

		UGroomBindingAsset* NewGroomBinding = DuplicateObject<UGroomBindingAsset>(GroomBinding, GetTransientPackage());
		NewGroomBinding->ClearFlags(RF_Public | RF_Standalone);

		NewGroomBinding->SetTargetSkeletalMesh(FitPair.Value.TargetMesh);

		// This asset is only used for RBF deformer, it is not meant to be saved.
		UGroomAsset* NewGroom = DuplicateObject<UGroomAsset>(SourceGroom, GetTransientPackage());

		// Bake RBF transforms. This needs to happen before decimation to match the mesh vertices
			
		// The RBF deformer doesn't support decimation in the interpolation data
		// (decimation in FHairLODSettings doesn't affect it), so we need to remove any
		// decimation first and then restore it afterwards.
		TArray<FHairGroupsInterpolation>& NewGroomInterpolationData = NewGroom->GetHairGroupsInterpolation();

		bool bSourceGroomHasDecimation = false;
		for (FHairGroupsInterpolation& Interpolation : NewGroomInterpolationData)
		{
			if (Interpolation.DecimationSettings.CurveDecimation < 1.0f
				|| Interpolation.DecimationSettings.VertexDecimation < 1.0f)
			{
				Interpolation.DecimationSettings.CurveDecimation = 1.0f;
				Interpolation.DecimationSettings.VertexDecimation = 1.0f;
				bSourceGroomHasDecimation = true;
			}
		}

		// Duplicate card static meshes so they can be deformed by RBF.
		// These are transient - only the final skeletal meshes are stored.
		{
			TArray<FHairGroupsCardsSourceDescription>& CardsArray = NewGroom->GetHairGroupsCards();
			for (FHairGroupsCardsSourceDescription& CardDesc : CardsArray)
			{
				if (UStaticMesh* OriginalCardMesh = CardDesc.GetMesh())
				{
					UStaticMesh* DuplicatedCardMesh = DuplicateObject<UStaticMesh>(
						OriginalCardMesh,
						GetTransientPackage());

					// Assign the duplicated mesh back to the card description
					CardDesc.ImportedMesh = DuplicatedCardMesh;

					// Make sure any async compilation is finished and the static mesh code is 
					// expecting us to to modify the meshes.
					DuplicatedCardMesh->ConditionalPostLoad();
					DuplicatedCardMesh->PreEditChange(nullptr);
				}
			}

			// Same for helmets - RBF deforms FHairGroupsMeshesSourceDescription entries too,
			// so we need transient duplicates to avoid marking the source groom dirty.
			TArray<FHairGroupsMeshesSourceDescription>& HelmetsArray = NewGroom->GetHairGroupsMeshes();
			for (FHairGroupsMeshesSourceDescription& HelmetDesc : HelmetsArray)
			{
				if (UStaticMesh* OriginalHelmetMesh = HelmetDesc.ImportedMesh)
				{
					UStaticMesh* DuplicatedHelmetMesh = DuplicateObject<UStaticMesh>(
						OriginalHelmetMesh,
						GetTransientPackage());

					HelmetDesc.ImportedMesh = DuplicatedHelmetMesh;

					DuplicatedHelmetMesh->ConditionalPostLoad();
					DuplicatedHelmetMesh->PreEditChange(nullptr);
				}
			}
		}

		if (bSourceGroomHasDecimation)
		{
			// GetRBFDeformedGroomAsset expects the binding to be set to the source
			// groom, so this is set here instead of passing NewGroom directly into
			// GetRBFDeformedGroomAsset.
			NewGroomBinding->SetGroom(NewGroom);

			// NewGroom will be used as the source groom, so we need to fire a post
			// edit change event to rebuild the derived data without decimation.
			FPropertyChangedEvent Event(FHairDecimationSettings::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FHairDecimationSettings, CurveDecimation)));
			NewGroom->PostEditChangeProperty(Event);
		}

		// Null pointers are allowed here, so this will work even if there is no source mesh
		FAssetCompilingManager::Get().FinishCompilationForObjects({ NewGroomBinding->GetSourceSkeletalMesh(), NewGroomBinding->GetTargetSkeletalMesh() });

		FGroomFitJob& Job = GroomFitJobs.AddDefaulted_GetRef();
		Job.TargetItemKey = FitPair.Key;
		Job.TargetMesh = FitPair.Value.TargetMesh;
		Job.NewGroomBinding = NewGroomBinding;
		Job.NewGroom = NewGroom;
		Job.GroomMeshes = CollectGroomMeshes(NewGroom);
	}

	ParallelFor(TEXT("MetaHuman Crowd Groom Fitting"), GroomFitJobs.Num(), 1, 
		[&GroomFitJobs, TargetPlatform, &Params](int32 JobIndex)
		{
			// Mask modulation not used atm
			FTextureSource* MaskSource = nullptr;
			float MaskScale = 0.0f;

			// Bake the RBF transformation within the groom asset
			FGroomRBFDeformer().GetRBFDeformedGroomAsset(
				GroomFitJobs[JobIndex].NewGroomBinding->GetGroom(),
				GroomFitJobs[JobIndex].NewGroomBinding,
				MaskSource,
				MaskScale,
				GroomFitJobs[JobIndex].NewGroom,
				TargetPlatform,
				/* bShouldBuildStaticMeshes */ false);
		});

	for (FGroomFitJob& Job : GroomFitJobs)
	{
		using namespace UE::MetaHuman::Private;


		// Group groom meshes by LODIndex - meshes from different groom groups but same LOD get merged.
		TMap<int32, TArray<int32>> LODToGroomMeshIndices;
		for (int32 GroomMeshIndex = 0; GroomMeshIndex < Job.GroomMeshes.Num(); ++GroomMeshIndex)
		{
			if (Job.GroomMeshes[GroomMeshIndex].Mesh)
			{
				LODToGroomMeshIndices.FindOrAdd(Job.GroomMeshes[GroomMeshIndex].LODIndex).Add(GroomMeshIndex);
			}
		}

		int32 MaxGroomLODIndex = -1;
		for (const TPair<int32, TArray<int32>>& LODPair : LODToGroomMeshIndices)
		{
			MaxGroomLODIndex = FMath::Max(MaxGroomLODIndex, LODPair.Key);
		}

		TArray<FMeshDescription> MergedMeshLODs;
		MergedMeshLODs.AddDefaulted(MaxGroomLODIndex + 1);

		TArray<TArray<FGroomMeshInfo>> LODGroomMeshInfosByLOD;
		LODGroomMeshInfosByLOD.SetNum(MaxGroomLODIndex + 1);

		for (const TPair<int32, TArray<int32>>& LODPair : LODToGroomMeshIndices)
		{
			const int32 GroomLODIndex = LODPair.Key;
			const TArray<int32>& GroomMeshIndices = LODPair.Value;

			// Collect static meshes and the matching FGroomMeshInfos for this LOD
			TArray<UStaticMesh*> LODStaticMeshes;
			TArray<FGroomMeshInfo>& LODGroomMeshInfos = LODGroomMeshInfosByLOD[GroomLODIndex];
			LODStaticMeshes.Reserve(GroomMeshIndices.Num());
			LODGroomMeshInfos.Reserve(GroomMeshIndices.Num());
			for (int32 GroomMeshIndex : GroomMeshIndices)
			{
				LODStaticMeshes.Add(Job.GroomMeshes[GroomMeshIndex].Mesh);
				LODGroomMeshInfos.Add(Job.GroomMeshes[GroomMeshIndex]);
			}

			// Merge mesh descriptions directly - avoids building static mesh render data for a merged mesh
			TArray<FSkeletalMaterial> UnusedLocalMaterials;
			TUniquePtr<FMeshDescription> MergedMeshDesc = MergeMeshDescriptions(LODStaticMeshes, UnusedLocalMaterials);

			if (!MergedMeshDesc)
			{
				continue;
			}

			MergedMeshLODs[GroomLODIndex] = MoveTemp(*MergedMeshDesc);
		}

		// Build per-variant materials. Each variant's MICs live in their own array so the
		// actor and instanced skeletal meshes never share a UMaterialInstanceConstant.
		// MIC names embed the consuming-mesh output-LOD position (= index in the variant's
		// LOD list) plus a Card/Helmet kind tag from FGroomMeshInfo::bIsHelmet.
		const FString HeadAssetName = Job.TargetItemKey.ToAssetNameString();

		TArray<FSkeletalMaterial> ActorMaterials;
		TArray<TArray<int32>> ActorLODMaterialMaps;
		BuildVariantMaterials(
			LODGroomMeshInfosByLOD,
			GroomBuildInput.ActorFaceLODs,
			LoadedCardsMaterial,
			LoadedHelmetsMaterial,
			Params.OuterForGeneratedObjects,
			GroomBuildOutput.GroomAssetName,
			HeadAssetName,
			TEXT("Actor"),
			nullptr,
			ActorMaterials,
			ActorLODMaterialMaps);

		TArray<FSkeletalMaterial> InstancedMaterials;
		TArray<TArray<int32>> InstancedLODMaterialMaps;
		BuildVariantMaterials(
			LODGroomMeshInfosByLOD,
			GroomBuildInput.InstancedFaceLODs,
			LoadedCardsMaterial,
			LoadedHelmetsMaterial,
			Params.OuterForGeneratedObjects,
			GroomBuildOutput.GroomAssetName,
			HeadAssetName,
			TEXT("Inst"),
			&RuntimePipeline->InstancedComponentOverrideMaterials,
			InstancedMaterials,
			InstancedLODMaterialMaps);

		TArray<FGroomCardConversionParams> PerLODParams;
		PerLODParams.Init(CardConversionParams, MergedMeshLODs.Num());
		for (const TPair<int32, TArray<int32>>& LODPair : LODToGroomMeshIndices)
		{
			const TArray<int32>& GroomMeshIndices = LODPair.Value;
			// Cards-or-helmet partition is uniform per LOD (see CollectGroomMeshes), so
			// checking the first entry is sufficient.
			const bool bIsHelmetLOD = !GroomMeshIndices.IsEmpty() && Job.GroomMeshes[GroomMeshIndices[0]].bIsHelmet;
			if (bIsHelmetLOD && PerLODParams[LODPair.Key].SkinWeightMethod != EMetaHumanCrowdGroomSkinWeightCopyMethod::None)
			{
				PerLODParams[LODPair.Key].SkinWeightMethod = EMetaHumanCrowdGroomSkinWeightCopyMethod::SingleBone;
			}
		}

		// Bake skin weights onto the merged card mesh descriptions and package directly into
		// a geometry bundle.
		FMetaHumanCrowdMeshGeometryBundle GroomBundle;

		if (!TryPrepareGroomGeometryBundle(
			MoveTemp(MergedMeshLODs),
			MoveTemp(ActorMaterials),
			MoveTemp(ActorLODMaterialMaps),
			Job.TargetMesh,
			PerLODParams,
			GroomBundle))
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning,
				"Crowd Groom pipeline: bundle preparation failed for {Target}",
				Job.TargetMesh->GetPathName());
			continue;
		}

		// Cache the geometry for future runs
		{
			const FString TargetMeshDerivedDataKey = Job.TargetMesh->GetDerivedDataKey();
			const UE::DerivedData::FCacheKey CacheKey = MakeGroomFittingCacheKey(
				TargetMeshDerivedDataKey, GroomBinding, SourceGroom, CardConversionParams, LoadedCardsMaterial, Params.BuildCacheGuid);
			StoreGroomBundleInDDC(CacheKey, GroomBundle.MeshDescriptions, Job.TargetMesh);
		}

		// Runtime AssembleItem reads the actor variant's Materials for ProcessAssemblyParameters,
		// so feed GenerateAssemblyParameters from the actor bundle's materials.
		const TArray<FSkeletalMaterial>& BundleMaterials = GroomBuildOutput.ItemToGroomGeometryMap.Add(Job.TargetItemKey, MoveTemp(GroomBundle)).Materials;

		FMetaHumanCrowdGroomMaterialOverride& InstancedOverride = GroomBuildOutput.InstancedMaterialOverrides.Add(Job.TargetItemKey);
		InstancedOverride.Materials = MoveTemp(InstancedMaterials);
		InstancedOverride.LODMaterialMaps = MoveTemp(InstancedLODMaterialMaps);

		FMetaHumanCrowdGroomBakedTextureEntry BakedEntry;
		if (BuildBakedGroomTextureEntry(
				LoadedBakedGroomTexture,
				GroomBuildInput.PipelineSlotName,
				EffectiveGroomTextureMinLOD,
				GroomBuildInput.ActorFaceLODs,
				GroomBuildInput.InstancedFaceLODs,
				BakedEntry))
		{
			GroomBuildOutput.ItemToBakedGroomTexture.Add(Job.TargetItemKey, MoveTemp(BakedEntry));
		}

		UE::MetaHuman::MaterialUtils::GenerateAssemblyParameters(
			{},
			RuntimePipeline->RuntimeMaterialParameters,
			BundleMaterials.Num(),
			UE::MetaHuman::MaterialUtils::MakeFetchSlotNameDelegate(BundleMaterials),
			UE::MetaHuman::MaterialUtils::MakeFetchSlotMaterialDelegate(BundleMaterials),
			GroomBuiltData.AssemblyParameters);
	}

	return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>(MoveTemp(BuiltDataResult));
}

TNotNull<const UMetaHumanCharacterEditorPipelineSpecification*> UMetaHumanCrowdGroomEditorPipeline::GetSpecification() const
{
	return Specification;
}
