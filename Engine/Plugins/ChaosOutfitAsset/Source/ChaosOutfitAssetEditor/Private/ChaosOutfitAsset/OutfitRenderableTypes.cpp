// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosOutfitAsset/OutfitRenderableTypes.h"
#include "ChaosClothAsset/ClothAssetBase.h"
#include "ChaosClothAsset/ClothComponent.h"
#include "ChaosClothAsset/ClothEngineTools.h"
#include "ChaosClothAsset/ClothSimulationModel.h"
#include "ChaosOutfitAsset/CollectionOutfitFacade.h"
#include "ChaosOutfitAsset/Outfit.h"
#include "ChaosOutfitAsset/OutfitAsset.h"
#include "Components/DynamicMeshComponent.h"
#include "Dataflow/DataflowRenderingViewMode.h"
#include "DataflowRendering/DataflowRenderableComponents.h"
#include "DataflowRendering/DataflowRenderableType.h"
#include "DataflowRendering/DataflowRenderableTypeInstance.h"
#include "DataflowRendering/DataflowRenderableTypeRegistry.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/Material.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "UObject/ObjectPtr.h"
#include "UObject/Package.h"

namespace UE::Chaos::OutfitAsset
{
	namespace Private
	{
		static constexpr EObjectFlags TransientFlags = RF_Transient | RF_TextExportTransient | RF_DuplicateTransient;

		static const FName Cloth3DSimViewName(TEXT("Cloth3DSimView"));
		static const FName ClothRenderViewName(TEXT("ClothRenderView"));

		static UMaterialInterface* GetSimViewMaterial()
		{
			static UMaterialInterface* const SimViewMaterial = Cast<UMaterial>(StaticLoadObject(
				UMaterial::StaticClass(), nullptr,
				TEXT("/Engine/EditorMaterials/Dataflow/DataflowTwoSidedVertexMaterial")));
			return SimViewMaterial;
		}

		static UChaosClothAssetBase* MakeOutfitAsset(const UChaosOutfit* Outfit)
		{
			UChaosOutfitAsset* const Asset = NewObject<UChaosOutfitAsset>(GetTransientPackage(), NAME_None, TransientFlags);
			Asset->Build(Outfit);
			return Asset;
		}

		static UChaosOutfit* FilterOutfitToSize(const UChaosOutfit* Outfit, const FString& SizeName)
		{
			UChaosOutfit* const Filtered = NewObject<UChaosOutfit>(GetTransientPackage(), NAME_None, TransientFlags);
			Filtered->Append(*Outfit, SizeName);
			return Filtered;
		}

		static void RenderAsClothComponent(UChaosClothAssetBase* Asset, FName ComponentName, UE::Dataflow::FRenderableComponents& OutComponents)
		{
			if (Asset && Asset->GetResourceForRendering())
			{
				if (UChaosClothComponent* const Component = OutComponents.AddNewComponent<UChaosClothComponent>(ComponentName))
				{
					Component->SetAsset(Asset);
				}
			}
		}

		/**
		 * Build a FDynamicMesh3 from the asset's cloth simulation models (ground truth).
		 * Aggregates all pieces into a single mesh. Returns empty mesh if no valid sim data.
		 */
		static UE::Geometry::FDynamicMesh3 BuildSimModelDynamicMesh(const UChaosClothAssetBase& Asset)
		{
			UE::Geometry::FDynamicMesh3 DynamicMesh;
			DynamicMesh.EnableAttributes();
			UE::Geometry::FDynamicMeshNormalOverlay* const NormalOverlay = DynamicMesh.Attributes()->PrimaryNormals();

			constexpr int32 LodIndex = 0;
			const int32 NumModels = Asset.GetNumClothSimulationModels();
			for (int32 ModelIndex = 0; ModelIndex < NumModels; ++ModelIndex)
			{
				const TSharedPtr<const FChaosClothSimulationModel> Model = Asset.GetClothSimulationModel(ModelIndex);
				if (Model.IsValid() && Model->IsValidLodIndex(LodIndex))
				{
					const TConstArrayView<FVector3f> Positions = Model->GetPositions(LodIndex);
					const TConstArrayView<FVector3f> Normals = Model->GetNormals(LodIndex);
					const TConstArrayView<uint32> Indices = Model->GetIndices(LodIndex);
					check(Normals.Num() == Positions.Num());
					check(Indices.Num() % 3 == 0);

					if (!Positions.IsEmpty() && !Indices.IsEmpty())
					{
						const int32 VertexOffset = DynamicMesh.MaxVertexID();
						const int32 NormalOffset = NormalOverlay->MaxElementID();

						for (int32 VertexIndex = 0; VertexIndex < Positions.Num(); ++VertexIndex)
						{
							DynamicMesh.AppendVertex(FVector3d(Positions[VertexIndex]));
						}
						for (int32 NormalIndex = 0; NormalIndex < Normals.Num(); ++NormalIndex)
						{
							NormalOverlay->AppendElement(Normals[NormalIndex]);
						}
						for (int32 TriangleIndex = 0; TriangleIndex + 2 < Indices.Num(); TriangleIndex += 3)
						{
							checkSlow(Indices[TriangleIndex] < (uint32)Positions.Num());
							checkSlow(Indices[TriangleIndex + 1] < (uint32)Positions.Num());
							checkSlow(Indices[TriangleIndex + 2] < (uint32)Positions.Num());
							const int32 Vertex0 = VertexOffset + (int32)Indices[TriangleIndex];
							const int32 Vertex1 = VertexOffset + (int32)Indices[TriangleIndex + 1];
							const int32 Vertex2 = VertexOffset + (int32)Indices[TriangleIndex + 2];
							const int32 TriangleID = DynamicMesh.AppendTriangle(Vertex0, Vertex1, Vertex2);
							if (TriangleID >= 0)
							{
								const UE::Geometry::FIndex3i NormalTriangle(
									NormalOffset + (int32)Indices[TriangleIndex],
									NormalOffset + (int32)Indices[TriangleIndex + 1],
									NormalOffset + (int32)Indices[TriangleIndex + 2]);
								NormalOverlay->SetTriangle(TriangleID, NormalTriangle);
							}
						}
					}
				}
			}
			return DynamicMesh;
		}

		static void RenderAsDynamicMeshComponent(
			UE::Geometry::FDynamicMesh3&& DynamicMesh,
			FName ComponentName,
			UMaterialInterface* Material,
			UE::Dataflow::FRenderableComponents& OutComponents)
		{
			if (DynamicMesh.VertexCount())
			{
				// Center the mesh at local origin and store the centroid as the component world position
				// (This keeps the bounding sphere small and prevents it from extending to the world origin when the mesh is elevated)
				const UE::Geometry::FAxisAlignedBox3d MeshAABB = DynamicMesh.GetBounds(false);
				const FVector3d Centroid = MeshAABB.Center();
				for (const int32 VertexID : DynamicMesh.VertexIndicesItr())
				{
					DynamicMesh.SetVertex(VertexID, DynamicMesh.GetVertex(VertexID) - Centroid);
				}
				if (UDynamicMeshComponent* const Component = OutComponents.AddNewComponent<UDynamicMeshComponent>(ComponentName))
				{
					Component->SetRelativeLocation(FVector(Centroid));
					Component->SetCastShadow(false);
					Component->SetMesh(MoveTemp(DynamicMesh));
					Component->UpdateBounds();
					Component->SetOverrideRenderMaterial(Material);
				}
				return;
			}
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// UChaosOutfitAsset renderable types
	//////////////////////////////////////////////////////////////////////////

	/** Renders a UChaosOutfitAsset in 3DView (filtered to single size, with materials). */
	class FOutfitAssetSurfaceRenderableType : public UE::Dataflow::IRenderableType
	{
		UE_DATAFLOW_IRENDERABLE_PRIMARY_TYPE(TObjectPtr<UChaosOutfitAsset>, OutfitAsset);
		UE_DATAFLOW_IRENDERABLE_RENDER_GROUP(Surface);
		UE_DATAFLOW_IRENDERABLE_VIEW_MODE(UE::Dataflow::FDataflowConstruction3DViewMode);

		virtual bool CanRender(const UE::Dataflow::FRenderableTypeInstance& Instance) const override
		{
			return GetOutfitAsset(Instance, nullptr) != nullptr;
		}

		virtual void GetPrimitiveComponents(
			const UE::Dataflow::FRenderableTypeInstance& Instance,
			UE::Dataflow::FRenderableComponents& OutComponents) const override
		{
			if (const TObjectPtr<UChaosOutfitAsset> OutfitAsset = GetOutfitAsset(Instance, nullptr))
			{
				const FName ComponentName = Instance.GetComponentName(TEXT("Outfit_Surface"));
				FSkeletalMeshRenderData* const SourceRenderData = OutfitAsset->GetResourceForRendering();
				const bool bHasRenderData = SourceRenderData &&
					SourceRenderData->LODRenderData.Num() > 0 &&
					SourceRenderData->LODRenderData[0].GetTotalFaces() > 0;
				if (bHasRenderData)
				{
					static const FName CacheName(TEXT("OutfitAssetPreviewCopy"));
					TObjectPtr<UChaosClothAssetBase> PreviewAsset;
					if (Instance.HasUptoDateCachedValue(CacheName))
					{
						PreviewAsset = Instance.GetCachedValue<TObjectPtr<UChaosClothAssetBase>>(CacheName);
					}
					else
					{
						const UChaosClothAssetBase* const AssetBase = OutfitAsset.Get();
						if (UChaosClothAssetBase* const PreviewCopy = AssetBase->CreatePreviewAssetCopy(
							GetTransientPackage(), Private::TransientFlags))
						{
							PreviewAsset = PreviewCopy;
							Instance.CacheValue(TObjectPtr<UChaosClothAssetBase>(PreviewAsset), CacheName);
						}
						else
						{
							PreviewAsset = OutfitAsset.Get();
						}
					}
					Private::RenderAsClothComponent(PreviewAsset, ComponentName, OutComponents);
				}
				else
				{
					UE::Geometry::FDynamicMesh3 SimModelDynamicMesh = Private::BuildSimModelDynamicMesh(*OutfitAsset);
					Private::RenderAsDynamicMeshComponent(
						MoveTemp(SimModelDynamicMesh),
						ComponentName,
						UE::Chaos::ClothAsset::FClothEngineTools::GetSimPreviewMaterial(),
						OutComponents);
				}
			}
		}
	};

	/** Renders a UChaosOutfitAsset as sim mesh in Cloth3DSim. */
	class FOutfitAssetSimSurfaceRenderableType : public UE::Dataflow::IRenderableType
	{
		UE_DATAFLOW_IRENDERABLE_PRIMARY_TYPE(TObjectPtr<UChaosOutfitAsset>, OutfitAsset);
		UE_DATAFLOW_IRENDERABLE_RENDER_GROUP(SimSurface);

		virtual bool IsViewModeSupported(const UE::Dataflow::IDataflowConstructionViewMode& InViewMode) const override
		{
			return InViewMode.GetName() == Private::Cloth3DSimViewName;
		}

		virtual bool CanRender(const UE::Dataflow::FRenderableTypeInstance& Instance) const override
		{
			const UChaosClothAssetBase* const AssetBase = GetOutfitAsset(Instance, nullptr);
			return AssetBase && AssetBase->HasValidClothSimulationModels();
		}

		virtual void GetPrimitiveComponents(
			const UE::Dataflow::FRenderableTypeInstance& Instance,
			UE::Dataflow::FRenderableComponents& OutComponents) const override
		{
			if (const TObjectPtr<UChaosOutfitAsset> OutfitAsset = GetOutfitAsset(Instance, nullptr))
			{
				UE::Geometry::FDynamicMesh3 SimModelDynamicMesh = Private::BuildSimModelDynamicMesh(*OutfitAsset);
				Private::RenderAsDynamicMeshComponent(
					MoveTemp(SimModelDynamicMesh),
					Instance.GetComponentName(TEXT("OutfitAsset_Sim3D")),
					Private::GetSimViewMaterial(),
					OutComponents);
			}
		}
	};

	/** Renders a UChaosOutfitAsset in ClothRenderView (all sizes, with materials). */
	class FOutfitAssetRenderSurfaceRenderableType : public UE::Dataflow::IRenderableType
	{
		UE_DATAFLOW_IRENDERABLE_PRIMARY_TYPE(TObjectPtr<UChaosOutfitAsset>, OutfitAsset);
		UE_DATAFLOW_IRENDERABLE_RENDER_GROUP(RenderSurface);

		virtual bool IsViewModeSupported(const UE::Dataflow::IDataflowConstructionViewMode& InViewMode) const override
		{
			return InViewMode.GetName() == Private::ClothRenderViewName;
		}

		virtual bool CanRender(const UE::Dataflow::FRenderableTypeInstance& Instance) const override
		{
			const TObjectPtr<UChaosOutfitAsset> OutfitAsset = GetOutfitAsset(Instance, nullptr);
			return OutfitAsset && OutfitAsset->GetResourceForRendering();
		}

		virtual void GetPrimitiveComponents(
			const UE::Dataflow::FRenderableTypeInstance& Instance,
			UE::Dataflow::FRenderableComponents& OutComponents) const override
		{
			if (const TObjectPtr<UChaosOutfitAsset> OutfitAsset = GetOutfitAsset(Instance, nullptr))
			{
				Private::RenderAsClothComponent(OutfitAsset.Get(), Instance.GetComponentName(TEXT("Outfit_Render")), OutComponents);
			}
		}
	};

	//////////////////////////////////////////////////////////////////////////
	// UChaosOutfit renderable types
	//////////////////////////////////////////////////////////////////////////

	/**
	 * Renders a UChaosOutfit in 3DView (one component per body size, with materials).
	 * Shows render data via UChaosClothComponent when available; otherwise falls back to a
	 * non-skinned sim mesh built directly from the simulation model (avoids skinning artifacts).
	 */
	class FOutfitSurfaceRenderableType : public UE::Dataflow::IRenderableType
	{
		UE_DATAFLOW_IRENDERABLE_PRIMARY_TYPE(TObjectPtr<UChaosOutfit>, Outfit);
		UE_DATAFLOW_IRENDERABLE_RENDER_GROUP(Surface);
		UE_DATAFLOW_IRENDERABLE_VIEW_MODE(UE::Dataflow::FDataflowConstruction3DViewMode);

		virtual bool CanRender(const UE::Dataflow::FRenderableTypeInstance& Instance) const override
		{
			const TObjectPtr<UChaosOutfit> Outfit = GetOutfit(Instance, nullptr);
			return Outfit && Outfit->GetPieces().Num() > 0;
		}

		virtual void GetPrimitiveComponents(
			const UE::Dataflow::FRenderableTypeInstance& Instance,
			UE::Dataflow::FRenderableComponents& OutComponents) const override
		{
			const TObjectPtr<UChaosOutfit> Outfit = GetOutfit(Instance, nullptr);
			if (!Outfit)
			{
				return;
			}

			const FCollectionOutfitConstFacade OutfitFacade(Outfit->GetOutfitCollection());
			const int32 NumSizes = OutfitFacade.IsValid() ? OutfitFacade.GetNumBodySizes() : 0;

			UMaterialInterface* const SimFallbackMaterial = UE::Chaos::ClothAsset::FClothEngineTools::GetSimPreviewMaterial();

			auto GetOrCacheAsset = [&Instance](const UChaosOutfit* SourceOutfit, FName CacheName) -> UChaosClothAssetBase*
				{
					if (Instance.HasUptoDateCachedValue(CacheName))
					{
						return Instance.GetCachedValue<TObjectPtr<UChaosClothAssetBase>>(CacheName).Get();
					}
					const TObjectPtr<UChaosClothAssetBase> Asset = Private::MakeOutfitAsset(SourceOutfit);
					Instance.CacheValue(TObjectPtr<UChaosClothAssetBase>(Asset), CacheName);
					return Asset.Get();
				};

			auto RenderOneSize = [&OutComponents, SimFallbackMaterial](UChaosClothAssetBase* Asset, FName ComponentName)
				{
					if (Asset)
					{
						FSkeletalMeshRenderData* const RenderData = Asset->GetResourceForRendering();
						if (RenderData &&
							RenderData->LODRenderData.Num() > 0 &&
							RenderData->LODRenderData[0].GetTotalFaces() > 0)
						{
							Private::RenderAsClothComponent(Asset, ComponentName, OutComponents);
						}
						else
						{
							UE::Geometry::FDynamicMesh3 SimModelDynamicMesh = Private::BuildSimModelDynamicMesh(*Asset);
							Private::RenderAsDynamicMeshComponent(MoveTemp(SimModelDynamicMesh), ComponentName, SimFallbackMaterial, OutComponents);
						}
					}
				};

			if (NumSizes == 0)
			{
				static const FName CacheName(TEXT("OutfitSurfaceAsset"));
				UChaosClothAssetBase* const Asset = GetOrCacheAsset(Outfit, CacheName);
				RenderOneSize(Asset, Instance.GetComponentName(TEXT("Outfit_Surface")));
			}
			else
			{
				for (int32 SizeIndex = 0; SizeIndex < NumSizes; ++SizeIndex)
				{
					const FString& SizeName = OutfitFacade.GetBodySizeName(SizeIndex);
					const FName CacheName(*FString::Printf(TEXT("OutfitSurfaceAsset_%d"), SizeIndex));

					UChaosClothAssetBase* Asset;
					if (Instance.HasUptoDateCachedValue(CacheName))
					{
						Asset = Instance.GetCachedValue<TObjectPtr<UChaosClothAssetBase>>(CacheName).Get();
					}
					else
					{
						UChaosOutfit* const FilteredOutfit = Private::FilterOutfitToSize(Outfit, SizeName);
						const TObjectPtr<UChaosClothAssetBase> BuiltAsset = Private::MakeOutfitAsset(FilteredOutfit);
						Instance.CacheValue(TObjectPtr<UChaosClothAssetBase>(BuiltAsset), CacheName);
						Asset = BuiltAsset.Get();
					}
					RenderOneSize(Asset, Instance.GetComponentName(*FString::Printf(TEXT("Outfit_Surface_%s"), *SizeName)));
				}
			}
		}
	};

	/**
	 * Renders each body size of a UChaosOutfit as sim mesh in Cloth3DSim.
	 * Renders from the simulation model (ground truth) directly via a UDynamicMeshComponent.
	 */
	class FOutfitSimSurfaceRenderableType : public UE::Dataflow::IRenderableType
	{
		UE_DATAFLOW_IRENDERABLE_PRIMARY_TYPE(TObjectPtr<UChaosOutfit>, Outfit);
		UE_DATAFLOW_IRENDERABLE_RENDER_GROUP(SimSurface);

		virtual bool IsViewModeSupported(const UE::Dataflow::IDataflowConstructionViewMode& InViewMode) const override
		{
			return InViewMode.GetName() == Private::Cloth3DSimViewName;
		}

		virtual bool CanRender(const UE::Dataflow::FRenderableTypeInstance& Instance) const override
		{
			const TObjectPtr<UChaosOutfit> Outfit = GetOutfit(Instance, nullptr);
			return Outfit && Outfit->GetPieces().Num() > 0;
		}

		virtual void GetPrimitiveComponents(
			const UE::Dataflow::FRenderableTypeInstance& Instance,
			UE::Dataflow::FRenderableComponents& OutComponents) const override
		{
			const TObjectPtr<UChaosOutfit> Outfit = GetOutfit(Instance, nullptr);
			if (!Outfit)
			{
				return;
			}

			const FCollectionOutfitConstFacade OutfitFacade(Outfit->GetOutfitCollection());
			const int32 NumSizes = OutfitFacade.IsValid() ? OutfitFacade.GetNumBodySizes() : 0;

			UMaterialInterface* const SimMaterial = Private::GetSimViewMaterial();

			auto RenderCachedAsset = [&OutComponents, SimMaterial](UChaosClothAssetBase* Asset, FName ComponentName)
				{
					if (Asset)
					{
						UE::Geometry::FDynamicMesh3 SimModelDynamicMesh = Private::BuildSimModelDynamicMesh(*Asset);
						Private::RenderAsDynamicMeshComponent(MoveTemp(SimModelDynamicMesh), ComponentName, SimMaterial, OutComponents);
					}
				};

			if (NumSizes == 0)
			{
				static const FName CacheName(TEXT("OutfitSimAsset"));

				UChaosClothAssetBase* Asset;
				if (Instance.HasUptoDateCachedValue(CacheName))
				{
					Asset = Instance.GetCachedValue<TObjectPtr<UChaosClothAssetBase>>(CacheName).Get();
				}
				else
				{
					const TObjectPtr<UChaosClothAssetBase> BuiltAsset = Private::MakeOutfitAsset(Outfit);
					Instance.CacheValue(TObjectPtr<UChaosClothAssetBase>(BuiltAsset), CacheName);
					Asset = BuiltAsset.Get();
				}
				RenderCachedAsset(Asset, Instance.GetComponentName(TEXT("Outfit_Sim3D")));
			}
			else
			{
				for (int32 SizeIndex = 0; SizeIndex < NumSizes; ++SizeIndex)
				{
					const FString& SizeName = OutfitFacade.GetBodySizeName(SizeIndex);
					const FName CacheName(*FString::Printf(TEXT("OutfitSimAsset_%d"), SizeIndex));

					UChaosClothAssetBase* Asset;
					if (Instance.HasUptoDateCachedValue(CacheName))
					{
						Asset = Instance.GetCachedValue<TObjectPtr<UChaosClothAssetBase>>(CacheName).Get();
					}
					else
					{
						UChaosOutfit* const FilteredOutfit = Private::FilterOutfitToSize(Outfit, SizeName);
						const TObjectPtr<UChaosClothAssetBase> BuiltAsset = Private::MakeOutfitAsset(FilteredOutfit);
						Instance.CacheValue(TObjectPtr<UChaosClothAssetBase>(BuiltAsset), CacheName);
						Asset = BuiltAsset.Get();
					}
					RenderCachedAsset(Asset, Instance.GetComponentName(*FString::Printf(TEXT("Outfit_Sim3D_%s"), *SizeName)));
				}
			}
		}
	};

	/** Renders each body size of a UChaosOutfit in ClothRenderView (with materials). */
	class FOutfitRenderSurfaceRenderableType : public UE::Dataflow::IRenderableType
	{
		UE_DATAFLOW_IRENDERABLE_PRIMARY_TYPE(TObjectPtr<UChaosOutfit>, Outfit);
		UE_DATAFLOW_IRENDERABLE_RENDER_GROUP(RenderSurface);

		virtual bool IsViewModeSupported(const UE::Dataflow::IDataflowConstructionViewMode& InViewMode) const override
		{
			return InViewMode.GetName() == Private::ClothRenderViewName;
		}

		virtual bool CanRender(const UE::Dataflow::FRenderableTypeInstance& Instance) const override
		{
			const TObjectPtr<UChaosOutfit> Outfit = GetOutfit(Instance, nullptr);
			return Outfit && Outfit->GetPieces().Num() > 0;
		}

		virtual void GetPrimitiveComponents(
			const UE::Dataflow::FRenderableTypeInstance& Instance,
			UE::Dataflow::FRenderableComponents& OutComponents) const override
		{
			const TObjectPtr<UChaosOutfit> Outfit = GetOutfit(Instance, nullptr);
			if (!Outfit)
			{
				return;
			}

			const FCollectionOutfitConstFacade OutfitFacade(Outfit->GetOutfitCollection());
			const int32 NumSizes = OutfitFacade.IsValid() ? OutfitFacade.GetNumBodySizes() : 0;

			if (NumSizes == 0)
			{
				static const FName CacheName(TEXT("OutfitRenderAsset"));

				TObjectPtr<UChaosClothAssetBase> RenderAsset;
				if (Instance.HasUptoDateCachedValue(CacheName))
				{
					RenderAsset = Instance.GetCachedValue<TObjectPtr<UChaosClothAssetBase>>(CacheName);
				}
				else
				{
					RenderAsset = Private::MakeOutfitAsset(Outfit);
					Instance.CacheValue(TObjectPtr<UChaosClothAssetBase>(RenderAsset), CacheName);
				}
				Private::RenderAsClothComponent(RenderAsset, Instance.GetComponentName(TEXT("Outfit_Render")), OutComponents);
			}
			else
			{
				for (int32 SizeIndex = 0; SizeIndex < NumSizes; ++SizeIndex)
				{
					const FString& SizeName = OutfitFacade.GetBodySizeName(SizeIndex);
					const FName CacheName(*FString::Printf(TEXT("OutfitRenderAsset_%d"), SizeIndex));

					TObjectPtr<UChaosClothAssetBase> RenderAsset;
					if (Instance.HasUptoDateCachedValue(CacheName))
					{
						RenderAsset = Instance.GetCachedValue<TObjectPtr<UChaosClothAssetBase>>(CacheName);
					}
					else
					{
						UChaosOutfit* const FilteredOutfit = Private::FilterOutfitToSize(Outfit, SizeName);
						RenderAsset = Private::MakeOutfitAsset(FilteredOutfit);
						Instance.CacheValue(TObjectPtr<UChaosClothAssetBase>(RenderAsset), CacheName);
					}
					Private::RenderAsClothComponent(
						RenderAsset,
						Instance.GetComponentName(*FString::Printf(TEXT("Outfit_Render_%s"), *SizeName)),
						OutComponents);
				}
			}
		}
	};

	void RegisterOutfitRenderableTypes()
	{
		UE_DATAFLOW_REGISTER_RENDERABLE_TYPE(FOutfitAssetSurfaceRenderableType);
		UE_DATAFLOW_REGISTER_RENDERABLE_TYPE(FOutfitAssetSimSurfaceRenderableType);
		UE_DATAFLOW_REGISTER_RENDERABLE_TYPE(FOutfitAssetRenderSurfaceRenderableType);
		UE_DATAFLOW_REGISTER_RENDERABLE_TYPE(FOutfitSurfaceRenderableType);
		UE_DATAFLOW_REGISTER_RENDERABLE_TYPE(FOutfitSimSurfaceRenderableType);
		UE_DATAFLOW_REGISTER_RENDERABLE_TYPE(FOutfitRenderSurfaceRenderableType);
	}
}
