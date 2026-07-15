// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "Rendering/NaniteResources.h"
#include "NaniteSceneProxy.h"

#define UE_API HAIRSTRANDSCORE_API

class UGroomAsset;
class UGroomComponent;

/////////////////////////////////////////////////////////////////////////////////////////
// FGroomAssetRenderData - Render Data

class FGroomAssetRenderData
{
public:
	/** Default constructor. */
	FGroomAssetRenderData() {}
	~FGroomAssetRenderData() {}

	void InitResources(ERHIFeatureLevel::Type InFeatureLevel, UGroomAsset* Owner);
	void ReleaseResources();

	bool HasValidNaniteData() const;

	// Build and cache render data
	void Cache(const ITargetPlatform* TargetPlatform, const UGroomAsset* InGroomAsset);

	// Gather Nanite materials
	void BuildMaterials(const UGroomAsset* InGroomAsset, bool bSetMaterialUsage = true);

	bool IsValid() const;

	void Serialize(FArchive& Ar, UObject* Owner, bool bCooked);
	void InitNaniteResources();

	// Accessors
	Nanite::FResources* GetResources();
	const Nanite::FMaterialAudit* GetMaterialAudit() const;

private:
	TPimplPtr<Nanite::FResources> NaniteResourcesPtr;
	TUniquePtr<Nanite::FMaterialAudit> NaniteMaterials;

	#if WITH_EDITORONLY_DATA
	/** The derived data key associated with this render data. */
	FString DerivedDataKey;
	uint32 NumCacheAttempts = 0;
	#endif

private:
	bool bIsInitialized = false;
};


/////////////////////////////////////////////////////////////////////////////////////////
// FGroomSceneProxy - Scene proxy

namespace Nanite
{

class FGroomSceneProxy: public FSceneProxyBase
{
public:
	using Super = FSceneProxyBase;

	UE_API FGroomSceneProxy(
		const FMaterialAudit& MaterialAudit,
		const UGroomComponent* InComponent,
		const UGroomAsset* InAsset,
		FGroomAssetRenderData* InRenderData,
		bool bAllowScaling = true);

	UE_API virtual ~FGroomSceneProxy();

public:
	// FPrimitiveSceneProxy interface.
	UE_API virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override;
	UE_API virtual void DestroyRenderThreadResources() override;
	UE_API virtual SIZE_T GetTypeHash() const override;
	UE_API virtual FPrimitiveViewRelevance	GetViewRelevance(const FSceneView* View) const override;
	#if WITH_EDITOR
	UE_API virtual HHitProxy* CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy> >& OutHitProxies) override;
	#endif
	UE_API virtual void DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) override;
	UE_API virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;

	UE_API virtual void GetPreSkinnedLocalBounds(FBoxSphereBounds& OutBounds) const override;

	virtual bool CanApplyStreamableRenderAssetScaleFactor() const override { return true; }

	UE_API virtual uint32 GetMemoryFootprint() const override;

	UE_API virtual FResourceMeshInfo GetResourceMeshInfo() const override;
	UE_API virtual FResourcePrimitiveInfo GetResourcePrimitiveInfo() const override;

	UE_API virtual FDesiredLODLevel GetDesiredLODLevel_RenderThread(const FSceneView* View) const final override;

	UE_API virtual uint8 GetCurrentFirstLODIdx_RenderThread() const final override;

protected:

	const FResources* Resources = nullptr;
	const UGroomAsset* GroomAsset = nullptr;

	FBoxSphereBounds PreSkinnedLocalBounds;

	uint32 NaniteResourceID = INDEX_NONE;
	uint32 NaniteHierarchyOffset = INDEX_NONE;
};

} // namespace Nanite

#undef UE_API