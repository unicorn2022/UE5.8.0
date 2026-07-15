// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalRenderNanite.h"
#include "Animation/MeshDeformerInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/InstancedSkinnedMeshComponent.h"
#include "RenderUtils.h"
#include "SkeletalRender.h"
#include "GPUSkinCache.h"
#include "Rendering/RenderCommandPipes.h"
#include "ShaderParameterUtils.h"
#include "SceneInterface.h"
#include "SkeletalMeshSceneProxy.h"
#include "RenderGraphUtils.h"
#include "RenderCore.h"
#include "Engine/SkinnedAssetCommon.h"
#include "SkeletalRenderGPUSkin.h"
#include "SkinnedMeshSceneProxyDesc.h"
#include "Async/ParallelFor.h"
#include <algorithm>

static int32 GSkeletalMeshThrottleNaniteRayTracingUpdates = 0;
static FAutoConsoleVariableRef CVarSkeletalMeshThrottleNaniteRayTracingUpdates(
	TEXT("r.SkeletalMesh.ThrottleNaniteRayTracingUpdates"),
	GSkeletalMeshThrottleNaniteRayTracingUpdates,
	TEXT("Throttles the number of Nanite ray tracing GPU skin cache updates to N per frame (excluding required updates due to LOD changes)"),
	ECVF_RenderThreadSafe);

int32 FDynamicSkelMeshObjectDataNanite::Reset()
{
	int32 Size = sizeof(FDynamicSkelMeshObjectDataNanite);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	ComponentSpaceTransforms.Reset();
	Size += ComponentSpaceTransforms.GetAllocatedSize();
#endif

	PreviousReferenceToLocal.Reset();
	Size += PreviousReferenceToLocal.GetAllocatedSize();

	ReferenceToLocal.Reset();
	Size += ReferenceToLocal.GetAllocatedSize();

	return Size;
}

void FDynamicSkelMeshObjectDataNanite::Init(
	USkinnedMeshComponent* InComponent,
	FSkeletalMeshRenderData* InRenderData,
	int32 InLODIndex,
	EPreviousBoneTransformUpdateMode InPreviousBoneTransformUpdateMode,
	FSkeletalMeshObjectNanite* InMeshObject)
{
	Init(
		FSkinnedMeshSceneProxyDynamicData(InComponent),
		InComponent->GetSkinnedAsset(),
		InRenderData,
		InLODIndex,
		InPreviousBoneTransformUpdateMode,
		InMeshObject);
}

void FDynamicSkelMeshObjectDataNanite::Init(
	const FSkinnedMeshSceneProxyDynamicData& InDynamicData,
	const USkinnedAsset* InSkinnedAsset,
	FSkeletalMeshRenderData* InRenderData,
	int32 InLODIndex,
	EPreviousBoneTransformUpdateMode InPreviousBoneTransformUpdateMode,
	FSkeletalMeshObjectNanite* InMeshObject
)
{
	LODIndex = InLODIndex;
	PreviousBoneTransformUpdateMode = InPreviousBoneTransformUpdateMode;

#if RHI_RAYTRACING
	RayTracingLODIndex = FMath::Clamp(FMath::Max(LODIndex, InMeshObject->RayTracingMinLOD), LODIndex, InRenderData->LODRenderData.Num() - 1);
#endif

#if !WITH_EDITOR
	// Skip storing previous Nanite bone transforms if we aren't in a Nanite raster pass that uses motion vectors.
	bNeedsBoneTransformsPrevious = InDynamicData.bDrawInGame;

	// Skip storing current Nanite bone transforms for if not visible in any Nanite raster pass or if ray tracing needs them.
	bNeedsBoneTransformsCurrent  = bNeedsBoneTransformsPrevious || InDynamicData.bCastsHiddenShadow || InDynamicData.bAffectIndirectLightingWhileHidden
#if RHI_RAYTRACING
		|| RayTracingLODIndex != LODIndex
#endif
		;

#else
	bNeedsBoneTransformsCurrent  = true;
	bNeedsBoneTransformsPrevious = true;
#endif

	if (InDynamicData.bUseReferenceToLocalMatrices)
	{
		if (bNeedsBoneTransformsCurrent
#if RHI_RAYTRACING
			|| LODIndex == RayTracingLODIndex
#endif
			)
		{
			ReferenceToLocal = InDynamicData.ReferenceToLocalMatrices;
		}

#if RHI_RAYTRACING
		if (RayTracingLODIndex != LODIndex)
		{
			ReferenceToLocalForRayTracing = InDynamicData.ReferenceToLocalMatrices;

			// It's possible that RayTracingLODIndex will reference bones that were unused by LODIndex. So we run an FK on unused bones in LODIndex.
			UpdateBonesRemovedByLOD(InMeshObject, ReferenceToLocalForRayTracing, InDynamicData, InSkinnedAsset, LODIndex, false);
		}
#endif

		if (bNeedsBoneTransformsPrevious)
		{
			if (PreviousBoneTransformUpdateMode == EPreviousBoneTransformUpdateMode::UpdatePrevious)
			{
				PreviousReferenceToLocal = InDynamicData.PreviousReferenceToLocalMatrices;
			}
			else if (PreviousBoneTransformUpdateMode == EPreviousBoneTransformUpdateMode::DuplicateCurrentToPrevious)
			{
				PreviousReferenceToLocal = ReferenceToLocal;
			}
		}
	}
	else
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		ComponentSpaceTransforms = InDynamicData.GetComponentSpaceTransforms();

		const bool bCalculateComponentSpaceTransformsFromLeader = ComponentSpaceTransforms.IsEmpty(); // This will be empty for follower components.
		TArray<FTransform>* const LeaderBoneMappedMeshComponentSpaceTransforms = bCalculateComponentSpaceTransformsFromLeader ? &ComponentSpaceTransforms : nullptr;
#else
		TArray<FTransform>* const LeaderBoneMappedMeshComponentSpaceTransforms = nullptr;
#endif

		if (bNeedsBoneTransformsCurrent || LODIndex == RayTracingLODIndex)
		{
			UpdateRefToLocalMatrices(ReferenceToLocal, InDynamicData, InSkinnedAsset, InRenderData, LODIndex, nullptr, LeaderBoneMappedMeshComponentSpaceTransforms);

			if (bNeedsBoneTransformsCurrent)
			{
				UpdateBonesRemovedByLOD(InMeshObject, ReferenceToLocal, InDynamicData, InSkinnedAsset, LODIndex, false);
			}
		}

#if RHI_RAYTRACING
		if (RayTracingLODIndex != LODIndex)
		{
			ReferenceToLocalForRayTracing = ReferenceToLocal;

			// It's possible that RayTracingLODIndex will reference bones that were unused by LODIndex. So we run an FK on unused bones in LODIndex.
			UpdateBonesRemovedByLOD(InMeshObject, ReferenceToLocalForRayTracing, InDynamicData, InSkinnedAsset, LODIndex, false);
		}
#endif

		if (bNeedsBoneTransformsPrevious)
		{
			if (PreviousBoneTransformUpdateMode == EPreviousBoneTransformUpdateMode::UpdatePrevious)
			{
				UpdatePreviousRefToLocalMatrices(PreviousReferenceToLocal, InDynamicData, InSkinnedAsset, InRenderData, LODIndex);
				UpdateBonesRemovedByLOD(InMeshObject, PreviousReferenceToLocal, InDynamicData, InSkinnedAsset, LODIndex, true);
			}
			else if (PreviousBoneTransformUpdateMode == EPreviousBoneTransformUpdateMode::DuplicateCurrentToPrevious)
			{
				PreviousReferenceToLocal = ReferenceToLocal;
			}
		}
	}

	BoneTransformFrameNumber = GFrameCounter;
	RevisionNumber = InDynamicData.GetBoneTransformRevisionNumber();
	PreviousRevisionNumber = InDynamicData.GetPreviousBoneTransformRevisionNumber();
	bRecreating = InDynamicData.IsRenderStateRecreating();
}

void FDynamicSkelMeshObjectDataNanite::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(sizeof(*this));
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(ReferenceToLocal.GetAllocatedSize());
}

//////////////////////////////////////////////////////////////////////////

class FSkeletalMeshUpdatePacketNanite : public TSkeletalMeshUpdatePacket<FSkeletalMeshObjectNanite, FDynamicSkelMeshObjectDataNanite>
{
public:
	void Init(const FInitializer& Initializer) override;
	void Add(FSkeletalMeshObjectNanite* MeshObject, FDynamicSkelMeshObjectDataNanite* DynamicData);
	void ProcessStage_Inline(FRHICommandList& RHICmdList, UE::Tasks::FTaskEvent& TaskEvent) override;
	void ProcessStage_SkinCache(FRHICommandList& RHICmdList, UE::Tasks::FTaskEvent& TaskEvent) override;
	void ProcessStage_Upload(FRHICommandList& RHICmdList) override;
	void Free(FDynamicSkelMeshObjectDataNanite* DynamicData);

private:
	struct FDynamicDataEntry
	{
		FDynamicSkelMeshObjectDataNanite* Current = nullptr;
		FDynamicSkelMeshObjectDataNanite* Previous = nullptr;
	};

	TArray<FDynamicDataEntry, FConcurrentLinearArrayAllocator> DynamicDatas;

#if RHI_RAYTRACING
	struct FSkinCacheEntryThrottled
	{
		FSkeletalMeshObjectNanite* MeshObject;
		uint32 BoneFrameDelta;
		uint32 BoneFrameNumber;
	};

	TArray<FSkinCacheEntryThrottled, FConcurrentLinearArrayAllocator> SkinCacheRayTracingThrottled;
	TArray<FSkeletalMeshObjectNanite*, FConcurrentLinearArrayAllocator> SkinCacheRayTracing;
	bool bRayTracingEnabled = false;
#endif
};

void FSkeletalMeshUpdatePacketNanite::Init(const FInitializer& Initializer)
{
	DynamicDatas.Reserve(Initializer.NumUpdates);
#if RHI_RAYTRACING
	bRayTracingEnabled = IsRayTracingEnabled();
	SkinCacheRayTracing.Reserve(Initializer.NumUpdates);

	if (GSkeletalMeshThrottleNaniteRayTracingUpdates > 0)
	{
		SkinCacheRayTracingThrottled.Reserve(Initializer.NumUpdates);
	}
#endif
}

void FSkeletalMeshUpdatePacketNanite::Add(FSkeletalMeshObjectNanite* MeshObject, FDynamicSkelMeshObjectDataNanite* DynamicData)
{
	DynamicDatas.Emplace(FDynamicDataEntry{ DynamicData, MeshObject->PreviousDynamicData });
	MeshObject->PreviousDynamicData = MeshObject->DynamicData;
	MeshObject->DynamicData = DynamicData;
	check(DynamicData);

#if RHI_RAYTRACING
	if (bRayTracingEnabled && IsSkinCacheForRayTracingSupported() && MeshObject->SkeletalMeshRenderData->bSupportRayTracing)
	{
		if (DynamicData->IsRequiredUpdate() || GSkeletalMeshThrottleNaniteRayTracingUpdates <= 0)
		{
			SkinCacheRayTracing.Emplace(MeshObject);
			MeshObject->LastRayTracingBoneTransformUpdate = DynamicData->BoneTransformFrameNumber;
		}
		else
		{
			SkinCacheRayTracingThrottled.Emplace(FSkinCacheEntryThrottled{ MeshObject, DynamicData->BoneTransformFrameNumber - MeshObject->LastRayTracingBoneTransformUpdate, DynamicData->BoneTransformFrameNumber });
		}
	}
#endif
}

void FSkeletalMeshUpdatePacketNanite::ProcessStage_Inline(FRHICommandList& RHICmdList, UE::Tasks::FTaskEvent& TaskEvent)
{
	if (!DynamicDatas.IsEmpty())
	{
		// On the render thread, sync the task at the end of the 'Inline' stage, as it's the final guaranteed sync point inside the scene update.
		TaskEvent.AddPrerequisites(UE::Tasks::Launch(UE_SOURCE_LOCATION, [DynamicDatas = MoveTemp(DynamicDatas)] () mutable
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ProcessNaniteDynamicDatas);

			for (FDynamicDataEntry Entry : DynamicDatas)
			{
				FDynamicSkelMeshObjectDataNanite::Release(Entry.Previous);
			}

		}, UE::Tasks::ETaskPriority::High));
	}
}

void FSkeletalMeshUpdatePacketNanite::ProcessStage_SkinCache(FRHICommandList& RHICmdList, UE::Tasks::FTaskEvent& TaskEvent)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Nanite);

#if RHI_RAYTRACING
	if (!SkinCacheRayTracing.IsEmpty())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkinCacheRayTracing);
		for (FSkeletalMeshObjectNanite* MeshObject : SkinCacheRayTracing)
		{
			MeshObject->ProcessUpdatedDynamicData(RHICmdList, GPUSkinCache);
		}
	}

	if (!SkinCacheRayTracingThrottled.IsEmpty())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkinCacheRayTracingThrottled);

		if (GSkeletalMeshThrottleNaniteRayTracingUpdates > 0 && !SkinCacheRayTracingThrottled.IsEmpty())
		{
			FSkinCacheEntryThrottled* StartIt = SkinCacheRayTracingThrottled.GetData();
			FSkinCacheEntryThrottled* EndIt   = SkinCacheRayTracingThrottled.GetData() + SkinCacheRayTracingThrottled.Num();
			FSkinCacheEntryThrottled* SortIt  = SkinCacheRayTracingThrottled.GetData() + FMath::Min(GSkeletalMeshThrottleNaniteRayTracingUpdates, SkinCacheRayTracingThrottled.Num());

			std::partial_sort(StartIt, SortIt, EndIt, [] (FSkinCacheEntryThrottled A, FSkinCacheEntryThrottled B)
			{
				return A.BoneFrameDelta > B.BoneFrameDelta;
			});

			if (SkinCacheRayTracingThrottled.Num() > GSkeletalMeshThrottleNaniteRayTracingUpdates)
			{
				SkinCacheRayTracingThrottled.SetNum(GSkeletalMeshThrottleNaniteRayTracingUpdates, EAllowShrinking::No);
			}
		}

		for (FSkinCacheEntryThrottled Entry : SkinCacheRayTracingThrottled)
		{
			Entry.MeshObject->ProcessUpdatedDynamicData(RHICmdList, GPUSkinCache);
			Entry.MeshObject->LastRayTracingBoneTransformUpdate = Entry.BoneFrameNumber;
		}
	}
#endif
}

void FSkeletalMeshUpdatePacketNanite::ProcessStage_Upload(FRHICommandList& RHICmdList)
{
#if RHI_RAYTRACING
	for (FSkeletalMeshObjectNanite* MeshObject : SkinCacheRayTracing)
	{
		MeshObject->UpdateBoneData(RHICmdList);
	}

	for (FSkinCacheEntryThrottled Entry : SkinCacheRayTracingThrottled)
	{
		Entry.MeshObject->UpdateBoneData(RHICmdList);
	}
#endif
}

void FSkeletalMeshUpdatePacketNanite::Free(FDynamicSkelMeshObjectDataNanite* DynamicData)
{
	FDynamicSkelMeshObjectDataNanite::Release(DynamicData);
}

REGISTER_SKELETAL_MESH_UPDATE_BACKEND(FSkeletalMeshUpdatePacketNanite);

//////////////////////////////////////////////////////////////////////////

FSkeletalMeshObjectNanite::FSkeletalMeshObjectNanite(USkinnedMeshComponent* InComponent, FSkeletalMeshRenderData* InRenderData, ERHIFeatureLevel::Type InFeatureLevel)
	: FSkeletalMeshObjectNanite(FSkinnedMeshSceneProxyDesc(InComponent), InRenderData, InFeatureLevel)
{ }

FSkeletalMeshObjectNanite::FSkeletalMeshObjectNanite(const FSkinnedMeshSceneProxyDesc& InMeshDesc, FSkeletalMeshRenderData* InRenderData, ERHIFeatureLevel::Type InFeatureLevel)
: FSkeletalMeshObject(InMeshDesc, InRenderData, InFeatureLevel)
, DynamicData(nullptr)
{
	BoneTransformStorageMode = InRenderData->HasUnifiedBoneMap() ? EBoneTransformStorageMode::BoneMap : EBoneTransformStorageMode::Direct;

#if RHI_RAYTRACING
	FSkeletalMeshObjectNanite* PreviousMeshObject = nullptr;
	if (InMeshDesc.PreviousMeshObject && InMeshDesc.PreviousMeshObject->IsNaniteMesh())
	{
		PreviousMeshObject = (FSkeletalMeshObjectNanite*)InMeshDesc.PreviousMeshObject;

		// Don't use re-create data if the mesh or feature level changed
		if (PreviousMeshObject->SkeletalMeshRenderData != InRenderData || PreviousMeshObject->FeatureLevel != InFeatureLevel)
		{
			PreviousMeshObject = nullptr;
		}
	}

	if (PreviousMeshObject)
	{
		// Transfer GPU skin cache from PreviousMeshObject -- needs to happen on render thread.  PreviousMeshObject is defer deleted, so it's safe to access it there.
		ENQUEUE_RENDER_COMMAND(ReleaseSkeletalMeshSkinCacheResources)(UE::RenderCommandPipe::SkeletalMesh,
			[this, PreviousMeshObject](FRHICommandList& RHICmdList)
			{
				SkinCacheEntryForRayTracing = PreviousMeshObject->SkinCacheEntryForRayTracing;

				// patch entries to point to new GPUSkin
				FGPUSkinCache::SetEntryGPUSkin(SkinCacheEntryForRayTracing, this);

				PreviousMeshObject->SkinCacheEntryForRayTracing = nullptr;
			}
		);
	}	
#endif

	for (int32 LODIndex = 0; LODIndex < InRenderData->LODRenderData.Num(); ++LODIndex)
	{
		new(LODs) FSkeletalMeshObjectLOD(InFeatureLevel, InRenderData, LODIndex);
	}

	InitResources(InMeshDesc);

	AuditMaterials(&InMeshDesc, NaniteMaterials, true /* Set material usage flags */);

	const bool bIsMaskingAllowed = Nanite::IsMaskingAllowed(InMeshDesc.GetWorld(), false /* force Nanite for masked */);

	bHasValidMaterials = NaniteMaterials.IsValid(bIsMaskingAllowed);

	checkf(InMeshDesc.Scene && InMeshDesc.Scene->GetSkeletalMeshUpdater(), TEXT("Scene cannot be null when creating a SkeletalMeshObject."));
	UpdateHandle = InMeshDesc.Scene->GetSkeletalMeshUpdater()->Create(this);
}

FSkeletalMeshObjectNanite::~FSkeletalMeshObjectNanite()
{
	FDynamicSkelMeshObjectDataNanite::Release(DynamicData);
	FDynamicSkelMeshObjectDataNanite::Release(PreviousDynamicData);
	DynamicData = nullptr;
	PreviousDynamicData = nullptr;
}

void FSkeletalMeshObjectNanite::InitResources(const FSkinnedMeshSceneProxyDesc& InMeshDesc)
{
	for (int32 LODIndex = 0; LODIndex < LODs.Num(); ++LODIndex)
	{
		FSkeletalMeshObjectLOD& LOD = LODs[LODIndex];

		// Skip LODs that have their render data stripped
		if (LOD.RenderData->LODRenderData[LODIndex].GetNumVertices() > 0)
		{
			const FSkelMeshComponentLODInfo* InitLODInfo = InMeshDesc.LODInfo.IsValidIndex(LODIndex) ? &InMeshDesc.LODInfo[LODIndex] : nullptr;
			LOD.InitResources(InitLODInfo, FeatureLevel);
		}
	}

#if RHI_RAYTRACING
	if (IsRayTracingEnabled() && bSupportRayTracing)
	{
		BeginInitResource(&RayTracingGeometry, &UE::RenderCommandPipe::SkeletalMesh);
	}
#endif
}

void FSkeletalMeshObjectNanite::ReleaseResources()
{
	UpdateHandle.Release();

	ENQUEUE_RENDER_COMMAND(FSkeletalMeshObjectNanite_ReleaseResources)(UE::RenderCommandPipe::SkeletalMesh,
		[this](FRHICommandList& RHICmdList)
	{
		for (FSkeletalMeshObjectLOD& LOD : LODs)
		{
			LOD.ReleaseResources();
		}

#if RHI_RAYTRACING
		RayTracingGeometry.ReleaseResource();
		FGPUSkinCache::Release(SkinCacheEntryForRayTracing);
#endif
	});
}

void FSkeletalMeshObjectNanite::Update(
	int32 InLODIndex,
	USkinnedMeshComponent* InComponent,
	const FMorphTargetWeightMap& InActiveMorphTargets,
	const TArray<float>& InMorphTargetWeights,
	EPreviousBoneTransformUpdateMode InPreviousBoneTransformUpdateMode,
	const FExternalMorphWeightData& InExternalMorphWeightData)
{
	if(InComponent)
	{
		Update(
			InLODIndex,
			FSkinnedMeshSceneProxyDynamicData(InComponent),
			InComponent->GetSceneProxy(),
			InComponent->GetSkinnedAsset(),
			InActiveMorphTargets,
			InMorphTargetWeights,
			InPreviousBoneTransformUpdateMode,
			InExternalMorphWeightData);
	}
}

void FSkeletalMeshObjectNanite::Update(int32 InLODIndex, const FSkinnedMeshSceneProxyDynamicData& InDynamicData, const FPrimitiveSceneProxy* InSceneProxy, const USkinnedAsset* InSkinnedAsset, const FMorphTargetWeightMap& InActiveMorphTargets, const TArray<float>& InMorphTargetWeights, EPreviousBoneTransformUpdateMode InPreviousBoneTransformUpdateMode, const FExternalMorphWeightData& InExternalMorphWeightData)
{
	FDynamicSkelMeshObjectDataNanite* DynamicDataToAssign = FDynamicSkelMeshObjectDataNanite::Acquire(InDynamicData.ComponentSpaceTransforms.Num());
	DynamicDataToAssign->Init(InDynamicData, InSkinnedAsset, SkeletalMeshRenderData, InLODIndex, InPreviousBoneTransformUpdateMode, this);

	UpdateHandle.Update(DynamicDataToAssign);
}

void FSkeletalMeshObjectNanite::ProcessUpdatedDynamicData(FRHICommandList& RHICmdList, FGPUSkinCache* GPUSkinCache)
{
	const int32 RayTracingLODIndex = DynamicData->RayTracingLODIndex;
	const TArray<FSkelMeshRenderSection>& Sections = GetRenderSections(RayTracingLODIndex);
	FSkeletalMeshObjectLOD& LOD = LODs[RayTracingLODIndex];

	const uint32 RevisionNumber = DynamicData->RevisionNumber;

	TArray<FGPUSkinCache::FProcessEntrySection, TInlineAllocator<8>> ProcessEntrySections;
	ProcessEntrySections.Reserve(Sections.Num());

	if (LOD.UnifiedBoneBuffer)
	{
		const bool bPrevious = false;
		LOD.UnifiedBoneBuffer->Allocate(RHICmdList, bPrevious);
	}

	for (int32 SectionIdx = 0; SectionIdx < Sections.Num(); SectionIdx++)
	{
		const FSkelMeshRenderSection& Section = Sections[SectionIdx];

		if (!Section.IsValid())
		{
			continue;
		}

		FGPUBaseSkinVertexFactory* VertexFactory = LOD.VertexFactories[SectionIdx].Get();
		FGPUBaseSkinVertexFactory::FShaderDataType& ShaderData = VertexFactory->GetShaderData();

		ShaderData.SetRevisionNumbers(RevisionNumber, RevisionNumber);
		ShaderData.UpdatedFrameNumber = DynamicData->BoneTransformFrameNumber;

		{
			const bool bPrevious = false;
			FVertexBufferAndSRV& BoneBuffer = ShaderData.GetBoneBufferForWriting(bPrevious);
			if (LOD.UnifiedBoneBuffer)
			{
				BoneBuffer = LOD.UnifiedBoneBuffer->GetBuffer(bPrevious);
			}
			else
			{
				ShaderData.AllocateBoneBuffer(RHICmdList, VertexFactory->GetBoneBufferSize(), BoneBuffer);
			}
		}

		ProcessEntrySections.Emplace(FGPUSkinCache::FProcessEntrySection
		{
			  .SourceVertexFactory = VertexFactory
			, .Section             = &Section
			, .SectionIndex        = SectionIdx
		});
	}

	GPUSkinCache->ProcessEntry(RHICmdList, FGPUSkinCache::FProcessEntryInputs
	{
		  .Mode                  = EGPUSkinCacheEntryMode::RayTracing
		, .Sections              = ProcessEntrySections
		, .Skin                  = this
		, .TargetVertexFactory   = LOD.PassthroughVertexFactory.Get()
		, .CurrentRevisionNumber = RevisionNumber
		, .LODIndex              = RayTracingLODIndex
		, .bRecreating           = DynamicData->bRecreating != 0

	}, SkinCacheEntryForRayTracing);
}

void FSkeletalMeshObjectNanite::UpdateBoneData(FRHICommandList& RHICmdList)
{
	const int32 RayTracingLODIndex = DynamicData->RayTracingLODIndex;
	const TArray<FSkelMeshRenderSection>& Sections = GetRenderSections(RayTracingLODIndex);
	const FName OwnerName = GetAssetPathName(RayTracingLODIndex);
	FSkeletalMeshObjectLOD& LOD = LODs[RayTracingLODIndex];

	TConstArrayView<FMatrix44f> ReferenceToLocalMatrices = DynamicData->GetReferenceToLocal();

	if (LOD.UnifiedBoneBuffer)
	{
		const bool bPrevious = false;
		LOD.UnifiedBoneBuffer->Update(RHICmdList, OwnerName, ReferenceToLocalMatrices, bPrevious);
	}
	else
	{
		for (int32 SectionIdx = 0; SectionIdx < Sections.Num(); SectionIdx++)
		{
			const FSkelMeshRenderSection& Section = Sections[SectionIdx];
			FGPUBaseSkinVertexFactory* VertexFactory = LOD.VertexFactories[SectionIdx].Get();
			FGPUBaseSkinVertexFactory::FShaderDataType& ShaderData = VertexFactory->GetShaderData();

			{
				const bool bPrevious = false;
				if (FRHIBuffer* VertexBufferRHI = ShaderData.GetBoneBufferForWriting(bPrevious).VertexBufferRHI)
				{
					ShaderData.UpdateBoneData(RHICmdList, OwnerName, ReferenceToLocalMatrices, Section.BoneMap, VertexBufferRHI);
				}
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////

const FVertexFactory* FSkeletalMeshObjectNanite::GetSkinVertexFactory(const FSceneView* View, int32 LODIndex, int32 ChunkIdx, ESkinVertexFactoryMode VFMode) const
{
	check(LODs.IsValidIndex(LODIndex));

	if (VFMode == ESkinVertexFactoryMode::RayTracing)
	{
		return LODs[LODIndex].PassthroughVertexFactory.Get();
	}

	return LODs[LODIndex].VertexFactories[ChunkIdx].Get();
}

const FVertexFactory* FSkeletalMeshObjectNanite::GetStaticSkinVertexFactory(int32 LODIndex, int32 ChunkIdx, ESkinVertexFactoryMode VFMode) const
{
	check(LODs.IsValidIndex(LODIndex));

	if (VFMode == ESkinVertexFactoryMode::RayTracing)
	{
		return LODs[LODIndex].PassthroughVertexFactory.Get();
	}

	return LODs[LODIndex].VertexFactories[ChunkIdx].Get();
}

TArray<FTransform>* FSkeletalMeshObjectNanite::GetComponentSpaceTransforms() const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (DynamicData)
	{
		return &(DynamicData->ComponentSpaceTransforms);
	}
	else
#endif
	{
		return nullptr;
	}
}

TConstArrayView<FMatrix44f> FSkeletalMeshObjectNanite::GetReferenceToLocalMatrices() const
{
	return DynamicData->ReferenceToLocal;
}

TConstArrayView<FMatrix44f> FSkeletalMeshObjectNanite::GetPrevReferenceToLocalMatrices() const
{
	if (DynamicData->bNeedsBoneTransformsPrevious)
	{
		// Too many revisions between previous / current to be useful. Fall back to current.
		if (DynamicData->RevisionNumber - DynamicData->PreviousRevisionNumber > 1)
		{
			return DynamicData->ReferenceToLocal;
		}

		if (!DynamicData->PreviousReferenceToLocal.IsEmpty())
		{
			return DynamicData->PreviousReferenceToLocal;
		}

		if (PreviousDynamicData)
		{
			return PreviousDynamicData->ReferenceToLocal;
		}
	}
	return {};
}

FSkeletalMeshObject::FBoneTransforms FSkeletalMeshObjectNanite::GetBoneTransforms() const
{
	return FBoneTransforms
	{
		  .CurrentTransforms      = GetReferenceToLocalMatrices()
		, .PreviousTransforms     = GetPrevReferenceToLocalMatrices()
		, .CurrentRevisionNumber  = DynamicData->RevisionNumber
		, .PreviousRevisionNumber = DynamicData->PreviousRevisionNumber
		, .UpdateMode             = DynamicData->PreviousBoneTransformUpdateMode
	};
}

FSkeletalMeshObject::FBoneBufferSRV FSkeletalMeshObjectNanite::GetSectionBoneBuffer(int32 InLODIndex, int32 InSectionIndex, bool bPreviousFrame) const
{
	FBoneBufferSRV Result;

	if (!DynamicData || !LODs.IsValidIndex(InLODIndex))
	{
		return Result;
	}

	const FSkeletalMeshObjectLOD& LOD = LODs[InLODIndex];
	if (LOD.VertexFactories.IsValidIndex(InSectionIndex) && LOD.VertexFactories[InSectionIndex])
	{
		const FGPUBaseSkinVertexFactory::FShaderDataType& ShaderData = LOD.VertexFactories[InSectionIndex]->GetShaderData();
		if (ShaderData.HasBoneBufferForReading(bPreviousFrame))
		{
			Result.SRV = ShaderData.GetBoneBufferForReading(bPreviousFrame).VertexBufferSRV;
		}
	}

	return Result;
}

int32 FSkeletalMeshObjectNanite::GetLOD() const
{
	// WorkingMinDesiredLODLevel can be a LOD that's not loaded, so need to clamp it to the first loaded LOD
	return FMath::Max<int32>(WorkingMinDesiredLODLevel, SkeletalMeshRenderData->CurrentFirstLODIdx);
}

bool FSkeletalMeshObjectNanite::HaveValidDynamicData() const
{
	return (DynamicData != nullptr);
}

void FSkeletalMeshObjectNanite::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(sizeof(*this));

	if (DynamicData)
	{
		DynamicData->GetResourceSizeEx(CumulativeResourceSize);
	}

	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(LODs.GetAllocatedSize());

	for (int32 Index = 0; Index < LODs.Num(); ++Index)
	{
		LODs[Index].GetResourceSizeEx(CumulativeResourceSize);
	}
}

void FSkeletalMeshObjectNanite::UpdateSkinWeightBuffer(USkinnedMeshComponent* InComponent)
{
	UpdateSkinWeightBuffer(InComponent->LODInfo);
}

void FSkeletalMeshObjectNanite::UpdateSkinWeightBuffer(const TArrayView<const FSkelMeshComponentLODInfo> InLODInfo)
{
	for (int32 LODIndex = 0; LODIndex < LODs.Num(); ++LODIndex)
	{
		FSkeletalMeshObjectLOD& LOD = LODs[LODIndex];

		// Skip LODs that have their render data stripped
		if (LOD.RenderData->LODRenderData[LODIndex].GetNumVertices() > 0)
		{
			const FSkelMeshComponentLODInfo* UpdateLODInfo = InLODInfo.IsValidIndex(LODIndex) ? &InLODInfo[LODIndex] : nullptr;
			LOD.UpdateSkinWeights(UpdateLODInfo);

			ENQUEUE_RENDER_COMMAND(UpdateSkinCacheSkinWeightBuffer)(UE::RenderCommandPipe::SkeletalMesh,
				[this](FRHICommandList& RHICmdList)
			{
				if (SkinCacheEntryForRayTracing)
				{
					FGPUSkinCache::UpdateSkinWeightBuffer(SkinCacheEntryForRayTracing);
				}
			});
		}
	}
}

FSkinningSceneExtensionProxy* FSkeletalMeshObjectNanite::CreateSceneExtensionProxy(const USkinnedAsset* InSkinnedAsset, bool bAllowScaling) const
{
	return new FSkinningSceneExtensionProxy(this, InSkinnedAsset, bAllowScaling);
}

void FSkeletalMeshObjectNanite::FSkeletalMeshObjectLOD::InitResources(const FSkelMeshComponentLODInfo* InLODInfo, ERHIFeatureLevel::Type InFeatureLevel)
{
	check(RenderData);
	check(RenderData->LODRenderData.IsValidIndex(LODIndex));

	FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];

	// Init vertex factories for ray tracing entry in skin cache
	if (IsRayTracingEnabled())
	{
		MeshObjectWeightBuffer = FSkeletalMeshObject::GetSkinWeightVertexBuffer(LODData, InLODInfo);

		FSkeletalMeshObjectGPUSkin::FVertexFactoryBuffers VertexBuffers;
		VertexBuffers.StaticVertexBuffers = &LODData.StaticVertexBuffers;
		VertexBuffers.ColorVertexBuffer = FSkeletalMeshObject::GetColorVertexBuffer(LODData, InLODInfo);
		VertexBuffers.SkinWeightVertexBuffer = MeshObjectWeightBuffer;
		VertexBuffers.MorphVertexBufferPool = nullptr; // MorphVertexBufferPool;
		VertexBuffers.APEXClothVertexBuffer = &LODData.ClothVertexBuffer;
		VertexBuffers.NumVertices = LODData.GetNumVertices();

		if (RenderData->HasUnifiedBoneMap())
		{
			UnifiedBoneBuffer = MakeUnique<FUnifiedBoneBuffer>(RenderData->GetUnifiedBoneMap());
		}

		ENQUEUE_RENDER_COMMAND(FSkeletalMeshObjectLOD_InitResources)(UE::RenderCommandPipe::SkeletalMesh,
			[this, &LODData, VertexBuffers = MoveTemp(VertexBuffers), InFeatureLevel](FRHICommandList& RHICmdList)
		{
			VertexFactories.Empty(LODData.RenderSections.Num());

			const FGPUSkinPassthroughVertexFactory::EVertexAttributeFlags VertexAttributeMask = FGPUSkinPassthroughVertexFactory::EVertexAttributeFlags::Position | FGPUSkinPassthroughVertexFactory::EVertexAttributeFlags::Tangent;
			const bool bUsedForPassthroughVertexFactory = true;
			const bool bUnifiedBoneBuffer = UnifiedBoneBuffer.IsValid();

			uint32 BoneOffset = 0;

			for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
			{
				const uint32 NumBones = Section.BoneMap.Num();

				FSkeletalMeshObjectGPUSkin::CreateVertexFactory(
					RHICmdList,
					VertexFactories,
					&PassthroughVertexFactory,
					VertexBuffers,
					InFeatureLevel,
					VertexAttributeMask,
					NumBones,
					BoneOffset,
					Section.BaseVertexIndex,
					bUsedForPassthroughVertexFactory,
					bUnifiedBoneBuffer);

				BoneOffset += bUnifiedBoneBuffer ? 0 : Section.BoneMap.Num();
			}
		});
	}

	bInitialized = true;
}

void FSkeletalMeshObjectNanite::FSkeletalMeshObjectLOD::ReleaseResources()
{
	bInitialized = false;

	for (auto& VertexFactory : VertexFactories)
	{
		if (VertexFactory)
		{
			VertexFactory->ReleaseResource();
		}
	}

	if (PassthroughVertexFactory)
	{
		PassthroughVertexFactory->ReleaseResource();
	}

	UnifiedBoneBuffer.Reset();
}

#if RHI_RAYTRACING
void FSkeletalMeshObjectNanite::UpdateRayTracingGeometry(FRHICommandListBase& RHICmdList, FSkeletalMeshLODRenderData& LODModel, uint32 LODIndex, TArray<FBufferRHIRef>& VertexBuffers)
{
	// TODO: Support WPO
	const bool bAnySegmentUsesWorldPositionOffset = false;

	FSkeletalMeshObjectGPUSkin::UpdateRayTracingGeometry_Internal(LODModel, LODIndex, VertexBuffers, RayTracingGeometry, bAnySegmentUsesWorldPositionOffset, this);
}
#endif

FSkinWeightVertexBuffer* FSkeletalMeshObjectNanite::GetSkinWeightVertexBuffer(int32 LODIndex) const
{
	checkSlow(LODs.IsValidIndex(LODIndex));
	return LODs[LODIndex].MeshObjectWeightBuffer;
}

void FSkeletalMeshObjectNanite::FSkeletalMeshObjectLOD::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
}

void FSkeletalMeshObjectNanite::FSkeletalMeshObjectLOD::UpdateSkinWeights(const FSkelMeshComponentLODInfo* InLODInfo)
{
	check(RenderData);
	check(RenderData->LODRenderData.IsValidIndex(LODIndex));

	FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
	MeshObjectWeightBuffer = FSkeletalMeshObject::GetSkinWeightVertexBuffer(LODData, InLODInfo);
}