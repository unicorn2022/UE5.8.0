// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProfilingDebugging/ResourceSize.h"
#include "RenderResource.h"
#include "RayTracingGeometry.h"
#include "ShaderParameters.h"
#include "Components/ExternalMorphSet.h"
#include "Components/SkinnedMeshComponent.h"
#include "GlobalShader.h"
#include "SkeletalMeshUpdater.h"
#include "SkeletalRenderPublic.h"
#include "ClothingSystemRuntimeTypes.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Animation/MeshDeformerGeometry.h"
#include "NaniteSceneProxy.h"
#include "InstancedSkeletalMeshObject.h"

class FPrimitiveDrawInterface;
class UMorphTarget;
class FSkeletalMeshObjectNanite;

/** 
* Stores the updated matrices needed to skin the verts.
* Created by the game thread and sent to the rendering thread as an update 
*/
class FDynamicSkelMeshObjectDataNanite final : public TPooledSkeletalMeshDynamicData<FDynamicSkelMeshObjectDataNanite>
{
	friend class TSkeletalMeshDynamicDataPool<FDynamicSkelMeshObjectDataNanite>;
	int32 Reset();

public:
	void Init(
		USkinnedMeshComponent* InComponent,
		FSkeletalMeshRenderData* InRenderData,
		int32 InLODIndex,
		EPreviousBoneTransformUpdateMode InPreviousBoneTransformUpdateMode,
		FSkeletalMeshObjectNanite* InMeshObject);

	void Init(
		const FSkinnedMeshSceneProxyDynamicData& InDynamicData,
		const USkinnedAsset* InSkinnedAsset,
		FSkeletalMeshRenderData* InRenderData,
		int32 InLODIndex,
		EPreviousBoneTransformUpdateMode InPreviousBoneTransformUpdateMode,
		FSkeletalMeshObjectNanite* InMeshObject);

	// Current reference pose to local space transforms
	TArray<FMatrix44f> ReferenceToLocal;
	TArray<FMatrix44f> ReferenceToLocalForRayTracing;
	TArray<FMatrix44f> PreviousReferenceToLocal;

	TConstArrayView<FMatrix44f> GetReferenceToLocal() const
	{
		return RayTracingLODIndex != LODIndex ? ReferenceToLocalForRayTracing : ReferenceToLocal;
	}

	bool IsRequiredUpdate() const
	{
		return PreviousBoneTransformUpdateMode != EPreviousBoneTransformUpdateMode::None;
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// Component space bone transforms
	TArray<FTransform> ComponentSpaceTransforms;
#endif

	uint32 BoneTransformFrameNumber;
	uint32 RevisionNumber;
	uint32 PreviousRevisionNumber;
	EPreviousBoneTransformUpdateMode PreviousBoneTransformUpdateMode;
	uint8 bRecreating : 1;
	uint8 bNeedsBoneTransformsCurrent : 1;
	uint8 bNeedsBoneTransformsPrevious : 1;

	// Current LOD for bones being updated
	int32 RayTracingLODIndex;

	// Returns the size of memory allocated by render data
	void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize);
};

class FSkeletalMeshObjectNanite final : public FSkeletalMeshObject
{
public:
	FSkeletalMeshObjectNanite(USkinnedMeshComponent* InComponent, FSkeletalMeshRenderData* InRenderData, ERHIFeatureLevel::Type InFeatureLevel);
	FSkeletalMeshObjectNanite(const FSkinnedMeshSceneProxyDesc& InMeshComponent, FSkeletalMeshRenderData* InSkelMeshRenderData, ERHIFeatureLevel::Type InFeatureLevel);
	virtual ~FSkeletalMeshObjectNanite();

	virtual void InitResources(const FSkinnedMeshSceneProxyDesc& InMeshDesc) override;
	virtual void ReleaseResources() override;
	
	void Update(
		int32 LODIndex,
		USkinnedMeshComponent* InComponent,
		const FMorphTargetWeightMap& InActiveMorphTargets,
		const TArray<float>& MorphTargetWeights,
		EPreviousBoneTransformUpdateMode PreviousBoneTransformUpdateMode,
		const FExternalMorphWeightData& InExternalMorphWeightData);
	
	virtual void Update(int32 LODIndex, const FSkinnedMeshSceneProxyDynamicData& InSkeletalMeshDynamicData, const FPrimitiveSceneProxy* InSceneProxy, const USkinnedAsset* InSkinnedAsset, const FMorphTargetWeightMap& InActiveMorphTargets, const TArray<float>& InMorphTargetWeights, EPreviousBoneTransformUpdateMode PreviousBoneTransformUpdateMode, const FExternalMorphWeightData& InExternalMorphWeightData);

	virtual FSkinningSceneExtensionProxy* CreateSceneExtensionProxy(const USkinnedAsset* InSkinnedAsset, bool bAllowScaling) const override;

	virtual const FVertexFactory* GetSkinVertexFactory(const FSceneView* View, int32 LODIndex, int32 ChunkIdx, ESkinVertexFactoryMode VFMode = ESkinVertexFactoryMode::Default) const override;
	virtual const FVertexFactory* GetStaticSkinVertexFactory(int32 LODIndex, int32 ChunkIdx, ESkinVertexFactoryMode VFMode = ESkinVertexFactoryMode::Default) const override;
	virtual TArray<FTransform>* GetComponentSpaceTransforms() const override;
	virtual TConstArrayView<FMatrix44f> GetReferenceToLocalMatrices() const override;
	virtual TConstArrayView<FMatrix44f> GetPrevReferenceToLocalMatrices() const override;
	FBoneTransforms GetBoneTransforms() const override;
	FBoneBufferSRV GetSectionBoneBuffer(int32 InLODIndex, int32 InSectionIndex, bool bPreviousFrame) const override;

	virtual int32 GetLOD() const override;

	virtual bool HaveValidDynamicData() const override;

	virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;

	void UpdateSkinWeightBuffer(USkinnedMeshComponent* InMeshComponent);
	virtual void UpdateSkinWeightBuffer(const TArrayView<const FSkelMeshComponentLODInfo> InLODInfo) override;

	virtual bool IsNaniteMesh() const override { return true; }
	virtual const Nanite::FMaterialAudit* GetNaniteMaterials() const override { return &NaniteMaterials; }

	virtual FSkinWeightVertexBuffer* GetSkinWeightVertexBuffer(int32 LODIndex) const;

#if RHI_RAYTRACING	
	FRayTracingGeometry RayTracingGeometry;
	
	virtual void UpdateRayTracingGeometry(FRHICommandListBase& RHICmdList, FSkeletalMeshLODRenderData& LODModel, uint32 LODIndex, TArray<FBufferRHIRef>& VertexBuffers) override;
	
	// GetRayTracingGeometry()->IsInitialized() is checked as a workaround for UE-92634. FSkeletalMeshSceneProxy's resources may have already been released, but proxy has not removed yet)
	FRayTracingGeometry* GetRayTracingGeometry() { return RayTracingGeometry.HasValidInitializer() && RayTracingGeometry.IsInitialized() ? & RayTracingGeometry : nullptr; }
	const FRayTracingGeometry* GetRayTracingGeometry() const { return RayTracingGeometry.HasValidInitializer() && RayTracingGeometry.IsInitialized() ? & RayTracingGeometry : nullptr; }

	virtual int32 GetRayTracingLOD() const override
	{
		if (DynamicData)
		{
			return DynamicData->RayTracingLODIndex;
		}
		else
		{
			return 0;
		}
	}
#endif

	inline bool HasValidMaterials() const
	{
		return bHasValidMaterials;
	}

private:
	FDynamicSkelMeshObjectDataNanite* DynamicData = nullptr;
	FDynamicSkelMeshObjectDataNanite* PreviousDynamicData = nullptr;

	void ProcessUpdatedDynamicData(FRHICommandList& RHICmdList, FGPUSkinCache* GPUSkinCache);
	void UpdateBoneData(FRHICommandList& RHICmdList);

	struct FSkeletalMeshObjectLOD
	{
		FSkeletalMeshRenderData* RenderData;
		int32 LODIndex;
		bool bInitialized;
		
		// Needed for skin cache update for ray tracing
		TArray<TUniquePtr<FGPUBaseSkinVertexFactory>> VertexFactories;
		TUniquePtr<FGPUSkinPassthroughVertexFactory> PassthroughVertexFactory;
		TUniquePtr<FUnifiedBoneBuffer> UnifiedBoneBuffer;

		FSkinWeightVertexBuffer* MeshObjectWeightBuffer = nullptr;

		FSkeletalMeshObjectLOD(ERHIFeatureLevel::Type InFeatureLevel, FSkeletalMeshRenderData* InRenderData, int32 InLOD)
		: RenderData(InRenderData)
		, LODIndex(InLOD)
		, bInitialized(false)
		{
		}

		void InitResources(const FSkelMeshComponentLODInfo* LODInfo, ERHIFeatureLevel::Type FeatureLevel);
		void ReleaseResources();
		void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize);
		void UpdateSkinWeights(const FSkelMeshComponentLODInfo* LODInfo);
	};

	TArray<FSkeletalMeshObjectLOD> LODs;

	FSkeletalMeshUpdateHandle UpdateHandle;

	Nanite::FMaterialAudit NaniteMaterials;
	bool bHasValidMaterials = false;
	uint32 LastRayTracingBoneTransformUpdate = INDEX_NONE;

	friend class FSkeletalMeshUpdatePacketNanite;
};