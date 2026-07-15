// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteGroomAsset.h"
#include "GroomAsset.h"
#include "NaniteGroomBuilder.h"
#include "GroomComponent.h"
#include "NaniteSceneProxy.h"
#include "Rendering/NaniteResourcesHelper.h"
#include "SceneView.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

/////////////////////////////////////////////////////////////////////////////////////////
// FGroomAssetRenderData

namespace Nanite
{
	namespace Private
	{
		template<>
		FString GetMaterialMeshName<UGroomAsset>(const UGroomAsset& Object)
		{
			return Object.GetName();
		}

		template<>
		bool IsMaterialSkeletalMesh<UGroomAsset>(const UGroomAsset& Object)
		{
			return false;
		}

		template<>
		TArray<FAuditMaterialSlotInfo, TInlineAllocator<32>> GetMaterialSlotInfos<UGroomAsset>(const UGroomAsset& Object)
		{
			TArray<FAuditMaterialSlotInfo, TInlineAllocator<32>> Out;
			const TArray<FName> MaterialSlotNames = Object.GetMaterialSlotNames();
			for (const FName& MaterialSlotName : MaterialSlotNames)
			{
				const int32 MaterialIndex = Object.GetMaterialIndex(MaterialSlotName);
				if (Object.GetHairGroupsMaterials().IsValidIndex(MaterialIndex))
				{
					FAuditMaterialSlotInfo SlotInfo;
					SlotInfo.Material = Object.GetHairGroupsMaterials()[MaterialIndex].Material;
					SlotInfo.SlotName = MaterialSlotName;
					SlotInfo.UVChannelData = FMeshUVChannelInfo(1.f);
					Out.Add(SlotInfo);
				}
			}
			return Out;
		}
	}
}

void FGroomAssetRenderData::InitResources(ERHIFeatureLevel::Type InFeatureLevel, UGroomAsset* Owner)
{
	check(NaniteResourcesPtr.IsValid());
	NaniteResourcesPtr->InitResources(Owner);
	bIsInitialized = true;
}

void FGroomAssetRenderData::ReleaseResources()
{
	if (bIsInitialized)
	{
		check(NaniteResourcesPtr.IsValid());
		NaniteResourcesPtr->ReleaseResources();
		bIsInitialized = false;
	}
}

bool FGroomAssetRenderData::HasValidNaniteData() const
{
	return NaniteResourcesPtr.IsValid() && NaniteResourcesPtr->PageStreamingStates.Num() > 0;
}

void FGroomAssetRenderData::Cache(const ITargetPlatform* TargetPlatform, const UGroomAsset* InGroomAsset)
{
	#if WITH_EDITOR
	if (InGroomAsset->GetOutermost()->bIsCookedForEditor)
	{
		// Don't cache for cooked packages
		return;
	}

	check(this == InGroomAsset->GetRenderData());

	::InitNaniteResources(NaniteResourcesPtr);
	check(NaniteResourcesPtr.IsValid());

	// 1. Build data
	FGroomAssetNaniteBuilder GroomBuilder;
	const bool bSucceed = GroomBuilder.Build(NaniteResourcesPtr, InGroomAsset);
	check(bSucceed);

	// 2. Gather nanite materials.
	// Set bSetMaterialUsage=false as Cache() is called from PostLoad, where referenced MaterialInstances may
	// still have RF_NeedPostLoad set. CheckMaterialUsage_Concurrent() -> SetMaterialUsage() opens an
	// FMaterialUpdateContext whose destructor iterates ALL loaded MIs via TObjectIterator and calls
	// InitStaticPermutation -> CacheShadersForResources, which asserts !RF_NeedPostLoad on every MI it
	// touches, not just the ones we called ConditionalPostLoad on. Passing false uses the read-only
	// NeedsSetMaterialUsage_Concurrent path instead, avoiding any shader recompile during PostLoad.
	// MATUSAGE_Nanite will be set correctly by FGroomSceneProxy::FGroomSceneProxy() at CreateSceneProxy
	// time, by which point all PostLoads have completed.
	BuildMaterials(InGroomAsset, /*bSetMaterialUsage=*/false);
	#endif
}

void FGroomAssetRenderData::BuildMaterials(const UGroomAsset* InGroomAsset, bool bSetMaterialUsage)
{
	// Gather nanite materials
	NaniteMaterials = MakeUnique<Nanite::FMaterialAudit>();
	Nanite::FNaniteResourcesHelper::AuditMaterials(InGroomAsset, *NaniteMaterials, bSetMaterialUsage);
}

Nanite::FResources* FGroomAssetRenderData::GetResources()
{
	return NaniteResourcesPtr.Get();
}

const Nanite::FMaterialAudit* FGroomAssetRenderData::GetMaterialAudit() const
{
	return NaniteMaterials.Get();
}

bool FGroomAssetRenderData::IsValid() const
{
	return NaniteResourcesPtr.IsValid();
}

void FGroomAssetRenderData::Serialize(FArchive& Ar, UObject* Owner, bool bCooked)
{
	check(NaniteResourcesPtr.IsValid());
	NaniteResourcesPtr->Serialize(Ar, Owner, bCooked);
}

void FGroomAssetRenderData::InitNaniteResources()
{
	::InitNaniteResources(NaniteResourcesPtr);
}

/////////////////////////////////////////////////////////////////////////////////////////
// Scene proxy
namespace Nanite 
{

FGroomSceneProxy::FGroomSceneProxy(
	const FMaterialAudit& MaterialAudit,
	const UGroomComponent* InComponent,
	const UGroomAsset* InAsset,
	FGroomAssetRenderData* InRenderData,
	bool bAllowScaling)
: FSceneProxyBase(InComponent)
, Resources(InRenderData->GetResources())
, GroomAsset(InAsset)
{
	LLM_SCOPE_BYTAG(Nanite);

	check(GroomAsset->GetRenderData());

	// This should always be valid.
	checkSlow(Resources && Resources->PageStreamingStates.Num() > 0);

	// Skinning is supported by this proxy
	bSkinnedMesh = false;

	// TODO_CURVE When adding binding and/or simuation, force the proxy to generate velociy vector
	//	bAlwaysHasVelocity = true;

	// Use fast path that does not update static draw lists.
	bStaticElementsAlwaysUseProxyPrimitiveUniformBuffer = true;

	// Nanite always uses GPUScene, so we can skip expensive primitive uniform buffer updates.
	bVFRequiresPrimitiveUniformBuffer = false;

	// For now no WPO support for groom
	PixelProgrammableDistance = 0.0f;

	bCompatibleWithLumenCardSharing = MaterialAudit.bCompatibleWithLumenCardSharing;
	PreSkinnedLocalBounds = InComponent->GetLocalBounds();
	
	const uint32 NumSections = GroomAsset->GetNumHairGroups();
	MaterialSections.SetNum(NumSections);
	for (uint32 SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex)
	{
		int32 MaterialIndex = InAsset->GetMaterialIndex(InAsset->GetHairGroupsRendering()[SectionIndex].MaterialSlotName);
		// If no material has been assigned to this section, fallback onto default material
		if (MaterialIndex == INDEX_NONE)
		{
			MaterialIndex = 0;
		}

		FMaterialSection& MaterialSection = MaterialSections[SectionIndex];
		MaterialSection.MaterialIndex = MaterialIndex;
		MaterialSection.bCastShadow = true;
		#if WITH_EDITORONLY_DATA
		MaterialSection.bSelected = false;
		#endif

		// Keep track of highest observed material index.
		MaterialMaxIndex = FMath::Max(MaterialSection.MaterialIndex, MaterialMaxIndex);

		MaterialSection.bHasVoxels = NaniteVoxelsSupported() && (Resources->VoxelMaterialsMask & (1ull << MaterialSection.MaterialIndex)) != 0;
		MaterialSection.bHasCurves = NaniteCurvesSupported() && Resources->NumInputCurves > 0;

		// If Section is hidden, do not cast shadow
		MaterialSection.bHidden = false;

		// If the material is NULL, or isn't flagged for use with skeletal meshes, it will be replaced by the default material.
		UMaterialInterface* ShadingMaterial = InComponent->GetMaterial(MaterialSection.MaterialIndex);

		bool bValidUsage = ShadingMaterial && ShadingMaterial->CheckMaterialUsage_Concurrent(MATUSAGE_Nanite);
		if (MaterialSection.bHasVoxels && ShadingMaterial && !ShadingMaterial->CheckMaterialUsage_Concurrent(MATUSAGE_Voxels))
		{
			bValidUsage = false;
		}
		if (MaterialSection.bHasCurves && ShadingMaterial && !ShadingMaterial->CheckMaterialUsage_Concurrent(MATUSAGE_Curves))
		{
			bValidUsage = false;
		}

		if (ShadingMaterial == nullptr || !bValidUsage)
		{
			ShadingMaterial = MaterialSection.bHidden ? GEngine->NaniteHiddenSectionMaterial.Get() : UMaterial::GetDefaultMaterial(MD_Surface);
		}

		MaterialSection.ShadingMaterialProxy = ShadingMaterial->GetRenderProxy();

		MaterialSection.LocalUVDensities = MaterialAudit.GetLocalUVDensities(MaterialSection.MaterialIndex);
	}


	// Now that the material sections are initialized, we can make material-dependent calculations
	OnMaterialsUpdated();

	// Nanite supports distance field representation for fully opaque meshes.
	bSupportsDistanceFieldRepresentation = false;

	FilterFlags = EFilterFlags::SkeletalMesh | EFilterFlags::NonStaticMobility;

	bReverseCulling = false;// InComponent->bReverseCulling;
	bOpaqueOrMasked = true; // Nanite only supports opaque
}

FGroomSceneProxy::~FGroomSceneProxy()
{
}

void FGroomSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
{
	check(Resources->RuntimeResourceID != INDEX_NONE && Resources->HierarchyOffset != INDEX_NONE);
}

void FGroomSceneProxy::DestroyRenderThreadResources()
{

}

SIZE_T FGroomSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

FPrimitiveViewRelevance	FGroomSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	LLM_SCOPE_BYTAG(Nanite);

	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View) && !!View->Family->EngineShowFlags.NaniteMeshes;
	Result.bShadowRelevance = IsShadowCast(View);
	Result.bRenderCustomDepth = Nanite::GetSupportsCustomDepthRendering() && ShouldRenderCustomDepth();
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();

	// Always render the Nanite mesh data with static relevance.
	Result.bStaticRelevance = true;

	// Should always be covered by constructor of Nanite scene proxy.
	Result.bRenderInMainPass = true;

	// TODO_CURVE: this might need to be revisted with simulation/binding, as the curve will no longer be static
	#if WITH_EDITOR
	Result.bEditorStaticSelectionRelevance = (WantsEditorEffects() || IsSelected() || IsHovered());
	#endif

	const auto& EngineShowFlags = View->Family->EngineShowFlags;

	const auto IsDynamic = [&]() -> bool
	{
		#if !(UE_BUILD_SHIPPING) || WITH_EDITOR
		return IsRichView(*View->Family)
			|| EngineShowFlags.Bones
			|| EngineShowFlags.Collision
			|| EngineShowFlags.Bounds
			|| IsSelected();
		#else
		return false;
		#endif
	};

	Result.bDynamicRelevance = IsDynamic();

	CombinedMaterialRelevance.SetPrimitiveViewRelevance(Result);
	Result.bVelocityRelevance = DrawsVelocity();

	return Result;
}

#if WITH_EDITOR

HHitProxy* FGroomSceneProxy::CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy>>& OutHitProxies)
{
	LLM_SCOPE_BYTAG(Nanite);

	// Create one proxy per section, all pointing to the actor
	for (int32 SectionIndex = 0; SectionIndex < MaterialSections.Num(); ++SectionIndex)
	{
		FMaterialSection& Section = MaterialSections[SectionIndex];
		HHitProxy* HitProxy = new ::HActor(Component->GetOwner(), Component, SectionIndex, SectionIndex);
		Section.HitProxy = HitProxy;
		OutHitProxies.Add(HitProxy);
	}

	return Super::CreateHitProxies(Component, OutHitProxies);
}

#endif

void FGroomSceneProxy::DrawStaticElements(FStaticPrimitiveDrawInterface* PDI)
{
	const FLightCacheInterface* LCI = nullptr;
	DrawStaticElementsInternal(PDI, LCI);
}

void FGroomSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	// Nothing for now, but could be used for debug drawing like bounds
}

void FGroomSceneProxy::GetPreSkinnedLocalBounds(FBoxSphereBounds& OutBounds) const
{
	OutBounds = PreSkinnedLocalBounds;
}

uint32 FGroomSceneProxy::GetMemoryFootprint() const
{
	return sizeof(*this) + GetAllocatedSize();
}

FResourceMeshInfo FGroomSceneProxy::GetResourceMeshInfo() const
{
	FResourceMeshInfo OutInfo;

	OutInfo.NumClusters = Resources->NumClusters;
	OutInfo.NumNodes = Resources->NumHierarchyNodes;
	OutInfo.NumVertices = Resources->NumInputVertices;
	OutInfo.NumTriangles = Resources->NumInputTriangles;
	OutInfo.NumCurves = Resources->NumInputCurves;
	OutInfo.NumMaterials = MaterialMaxIndex + 1;
	OutInfo.DebugName = GroomAsset->GetFName();

	OutInfo.NumResidentClusters = Resources->NumResidentClusters;

	OutInfo.bAssembly = Resources->AssemblyTransforms.Num() > 0;
	OutInfo.NumSegments = MaterialSections.Num(); 
	OutInfo.SegmentMapping.Init(INDEX_NONE, MaterialMaxIndex + 1);
	uint32 SectionIndex = 0;
	for (const FSceneProxyBase::FMaterialSection& MaterialSection : MaterialSections)
	{
		OutInfo.SegmentMapping[MaterialSection.MaterialIndex] = SectionIndex;
		++SectionIndex;
	}
	return MoveTemp(OutInfo);
}

FResourcePrimitiveInfo FGroomSceneProxy::GetResourcePrimitiveInfo() const
{
	return FResourcePrimitiveInfo(*Resources);
}

FDesiredLODLevel FGroomSceneProxy::GetDesiredLODLevel_RenderThread(const FSceneView* View) const
{
	return FDesiredLODLevel::CreateFixed(0);
}

uint8 FGroomSceneProxy::GetCurrentFirstLODIdx_RenderThread() const
{
	return 0;
}

} // namespace Nanite