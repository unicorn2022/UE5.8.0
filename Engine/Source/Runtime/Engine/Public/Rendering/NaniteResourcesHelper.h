// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NaniteSceneProxy.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "MaterialDomain.h"
#include "MaterialCachedData.h"
#include "MaterialShaderType.h"
#include "MeshUVChannelInfo.h"
#include "Engine/StaticMesh.h"
#include "UObject/Package.h"
#include "EngineLogs.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeRWLock.h"

class USkinnedMeshComponent;
struct FSkinnedMeshSceneProxyDesc;
struct FInstancedSkinnedMeshSceneProxyDesc;

namespace Nanite
{
	struct FMaterialAudit;

	/** Helper class used to share implementation for different Component types */
	class FNaniteResourcesHelper
	{
	public:
		template<class T>
		static bool HasValidNaniteData(const T& Component);

		template<class T>
		static bool ShouldCreateNaniteProxy(const T& Component, FMaterialAudit* OutNaniteMaterials);

		template<class T>
		static bool UseNaniteOverrideMaterials(const T& Component, bool bDoingMaterialAudit);

		template<class T>
		static FMaterialAudit& AuditMaterials(const T* Component, FMaterialAudit& Audit, bool bSetMaterialUsage);
	};

	namespace Private
	{
		struct FAuditMaterialSlotInfo
		{
			UMaterialInterface* Material;
			FName SlotName;
			FMeshUVChannelInfo UVChannelData;
		};

		template<class T>
		FString GetMaterialMeshName(const T& Object)
		{
			return Object.GetStaticMesh()->GetName();
		}

		template<class T>
		bool IsMaterialSkeletalMesh(const T& Object)
		{
			return false;
		}

		template<class T>
		TArray<FAuditMaterialSlotInfo, TInlineAllocator<32>> GetMaterialSlotInfos(const T& Object)
		{
			TArray<FAuditMaterialSlotInfo, TInlineAllocator<32>> Infos;

			if (UStaticMesh* StaticMesh = Object.GetStaticMesh())
			{
				TArray<FStaticMaterial>& StaticMaterials = StaticMesh->GetStaticMaterials();

				uint32 Index = 0;
				for (FStaticMaterial& Material : StaticMaterials)
				{
					Infos.Add({ Object.GetNaniteAuditMaterial(Index), Material.MaterialSlotName, Material.UVChannelData });
					Index++;
				}
			}

			return Infos;
		}

		template<>
		FString GetMaterialMeshName<USkinnedMeshComponent>(const USkinnedMeshComponent& Object);

		template<>
		FString GetMaterialMeshName<FSkinnedMeshSceneProxyDesc>(const FSkinnedMeshSceneProxyDesc& Object);

		template<>
		FString GetMaterialMeshName<FInstancedSkinnedMeshSceneProxyDesc>(const FInstancedSkinnedMeshSceneProxyDesc& Object);

		template<>
		bool IsMaterialSkeletalMesh<USkinnedMeshComponent>(const USkinnedMeshComponent& Object);

		template<>
		bool IsMaterialSkeletalMesh<FSkinnedMeshSceneProxyDesc>(const FSkinnedMeshSceneProxyDesc& Object);

		template<>
		bool IsMaterialSkeletalMesh<FInstancedSkinnedMeshSceneProxyDesc>(const FInstancedSkinnedMeshSceneProxyDesc& Object);

		template<>
		TArray<FAuditMaterialSlotInfo, TInlineAllocator<32>> GetMaterialSlotInfos<USkinnedMeshComponent>(const USkinnedMeshComponent& Object);

		template<>
		TArray<FAuditMaterialSlotInfo, TInlineAllocator<32>> GetMaterialSlotInfos<FSkinnedMeshSceneProxyDesc>(const FSkinnedMeshSceneProxyDesc& Object);

		template<>
		TArray<FAuditMaterialSlotInfo, TInlineAllocator<32>> GetMaterialSlotInfos<FInstancedSkinnedMeshSceneProxyDesc>(const FInstancedSkinnedMeshSceneProxyDesc& Object);
	}

#if !(UE_BUILD_SHIPPING) || WITH_EDITOR
	// Non-template dedup function shared across all AuditMaterials instantiations.
	// Uses 64-bit XXHash to avoid storing full strings while keeping collision probability negligible.
	// The set is capped to bound memory in long-running processes; resetting may re-emit some warnings.
	inline bool ShouldLogMaterialWarning(const FString& MaterialName, const FString& MeshName)
	{
		constexpr uint32 MaxEntries = 4096;
		static FRWLock RWLock;
		static TSet<uint64> WarnedCombinations;

		// Hash outside the lock — pure computation, no shared state.
		FXxHash64Builder HashBuilder;
		HashBuilder.Update(MaterialName.GetCharArray().GetData(), MaterialName.GetCharArray().Num() * sizeof(FString::ElementType));
		HashBuilder.Update(MeshName.GetCharArray().GetData(), MeshName.GetCharArray().Num() * sizeof(FString::ElementType));
		const uint64 Hash = HashBuilder.Finalize().Hash;

		// Fast path: read lock to check if we already warned (common case).
		{
			FReadScopeLock ReadLock(RWLock);
			if (WarnedCombinations.Contains(Hash))
			{
				return false;
			}
		}

		// Slow path: write lock to insert.
		{
			FWriteScopeLock WriteLock(RWLock);

			// Re-check under write lock — another thread may have inserted between our read and write.
			bool bAlreadyInSet = false;
			if (WarnedCombinations.Num() >= MaxEntries)
			{
				WarnedCombinations.Reset();
			}
			WarnedCombinations.Add(Hash, &bAlreadyInSet);
			return !bAlreadyInSet;
		}
	}
#endif //!(UE_BUILD_SHIPPING) || WITH_EDITOR

	template<class T>
	bool FNaniteResourcesHelper::ShouldCreateNaniteProxy(const T& Component, FMaterialAudit* OutNaniteMaterials)
	{
		// Whether or not to allow Nanite for this component
#if WITH_EDITORONLY_DATA
		const bool bForceFallback = Component.IsDisplayNaniteFallbackMesh();
#else
		const bool bForceFallback = false;
#endif

		if (bForceFallback || Component.IsDisallowNanite() || Component.IsForceDisableNanite())
		{
			// Regardless of the static mesh asset supporting Nanite, this component does not want Nanite to be used
			return false;
		}

		const EShaderPlatform ShaderPlatform = Component.GetScene() ? Component.GetScene()->GetShaderPlatform() : GMaxRHIShaderPlatform;

		if (!UseNanite(ShaderPlatform) || !Component.HasValidNaniteData())
		{
			return false;
		}

		{
			FMaterialAudit NaniteMaterials{};
			AuditMaterials(&Component, NaniteMaterials, OutNaniteMaterials != nullptr);

			const bool bIsMaskingAllowed = Nanite::IsMaskingAllowed(Component.GetWorld(), Component.IsForceNaniteForMasked());
			if (!NaniteMaterials.IsValid(bIsMaskingAllowed))
			{
				return false;
			}

			if (OutNaniteMaterials)
			{
				*OutNaniteMaterials = MoveTemp(NaniteMaterials);
			}
		}

		return true;
	}

	template<class T>
	bool FNaniteResourcesHelper::HasValidNaniteData(const T& Component)
	{
		const FResources* NaniteResources = Component.GetNaniteResources();
		return NaniteResources != nullptr ? NaniteResources->PageStreamingStates.Num() > 0 : false;
	}

	template<class T>
	bool FNaniteResourcesHelper::UseNaniteOverrideMaterials(const T& Component, bool bDoingMaterialAudit)
	{
		// Check for valid data on this SMC and support for Nanite material overrides
		return (bDoingMaterialAudit || FNaniteResourcesHelper::ShouldCreateNaniteProxy(Component, nullptr)) && Nanite::GEnableNaniteMaterialOverrides != 0;
	}

	template<class T>
	FMaterialAudit& FNaniteResourcesHelper::AuditMaterials(const T* Component, FMaterialAudit& Audit, bool bSetMaterialUsage)
	{
		static const auto NaniteForceEnableMeshesCvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Nanite.ForceEnableMeshes"));
		static const bool bNaniteForceEnableMeshes = NaniteForceEnableMeshesCvar && NaniteForceEnableMeshesCvar->GetValueOnAnyThread() != 0;

		static const auto CVarLumenDetectCardSharingCompatibility = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.LumenScene.SurfaceCache.DetectCardSharingCompatibility"));
		const bool bLumenDetectCardSharingCompatibility = CVarLumenDetectCardSharingCompatibility && CVarLumenDetectCardSharingCompatibility->GetValueOnAnyThread() != 0;

		Audit.bHasAnyError = false;
		Audit.Entries.Reset();

		if (Component != nullptr)
		{
			TArray<Private::FAuditMaterialSlotInfo, TInlineAllocator<32>> Slots = Nanite::Private::GetMaterialSlotInfos(*Component);

			Audit.bCompatibleWithLumenCardSharing = Slots.Num() > 0;

			uint32 Index = 0;
			for (const Private::FAuditMaterialSlotInfo& SlotInfo : Slots)
			{
				FMaterialAuditEntry& Entry = Audit.Entries.AddDefaulted_GetRef();
				Entry.MaterialSlotName = SlotInfo.SlotName;
				Entry.MaterialIndex = Index;
				Index++;
				Entry.Material = SlotInfo.Material;
				Entry.bHasNullMaterial = Entry.Material == nullptr;
				Entry.LocalUVDensities = FVector4f(
					SlotInfo.UVChannelData.LocalUVDensities[0],
					SlotInfo.UVChannelData.LocalUVDensities[1],
					SlotInfo.UVChannelData.LocalUVDensities[2],
					SlotInfo.UVChannelData.LocalUVDensities[3]
				);

				if (Entry.bHasNullMaterial)
				{
					// Never allow null materials, assign default instead
					Entry.Material = UMaterial::GetDefaultMaterial(MD_Surface);
				}

				const UMaterial* Material = Entry.Material->GetMaterial_Concurrent();
				check(Material != nullptr); // Should always be valid here

				const EBlendMode BlendMode = Entry.Material->GetBlendMode();

				bool bUsingCookedEditorData = false;
#if WITH_EDITORONLY_DATA
				bUsingCookedEditorData = Material->GetOutermost()->bIsCookedForEditor;
#endif
				bool bUsageSetSuccessfully = false;

				const FMaterialCachedExpressionData& CachedMaterialData = Material->GetCachedExpressionData();
				Entry.bHasVertexInterpolator = CachedMaterialData.bHasVertexInterpolator;
				Entry.bHasPerInstanceRandomID = CachedMaterialData.bHasPerInstanceRandom;
				Entry.bHasPerInstanceCustomData = CachedMaterialData.bHasPerInstanceCustomData;
				Entry.bHasVertexUVs = CachedMaterialData.bHasCustomizedUVs;
				Entry.bHasPixelDepthOffset = Material->HasPixelDepthOffsetConnected();
				Entry.bHasWorldPositionOffset = Material->HasVertexPositionOffsetConnected();
				Entry.bHasTessellationEnabled = Material->IsTessellationEnabled();
				Entry.bHasUnsupportedBlendMode = !IsSupportedBlendMode(BlendMode);
				Entry.bHasUnsupportedShadingModel = !IsSupportedShadingModel(Material->GetShadingModels());
				Entry.bHasInvalidUsage = (bUsingCookedEditorData || !bSetMaterialUsage) ? Entry.Material->NeedsSetMaterialUsage_Concurrent(bUsageSetSuccessfully, MATUSAGE_Nanite) : !Entry.Material->CheckMaterialUsage_Concurrent(MATUSAGE_Nanite);

				if (Private::IsMaterialSkeletalMesh(*Component))
				{
					Entry.bHasInvalidUsage |= (bUsingCookedEditorData || !bSetMaterialUsage) ? Entry.Material->NeedsSetMaterialUsage_Concurrent(bUsageSetSuccessfully, MATUSAGE_SkeletalMesh) : !Entry.Material->CheckMaterialUsage_Concurrent(MATUSAGE_SkeletalMesh);
				}

				Audit.bHasMasked |= (BlendMode == BLEND_Masked);
				Entry.bHasTranslucency = IsTranslucentOnlyBlendMode(BlendMode);

				if (Material->bIsSky)
				{
					// Sky material is a special case we want to skip
					Audit.bHasSky = true;
				}

				auto IsCompatibleWithLumenCardSharing = [&]()
				{
					if (Entry.Material->IsCompatibleWithLumenCardSharing())
					{
						// Material is explicitly marked as compatible
						return true;
					}

					return bLumenDetectCardSharingCompatibility
						&& !CachedMaterialData.bHasPerInstanceRandom
						&& !CachedMaterialData.bHasPerInstanceCustomData
						&& !CachedMaterialData.bHasWorldPosition;
				};

				if (!IsCompatibleWithLumenCardSharing())
				{
					Audit.bCompatibleWithLumenCardSharing = false;
				}

				Entry.bHasAnyError =
					Entry.bHasUnsupportedBlendMode |
					Entry.bHasUnsupportedShadingModel |
					Entry.bHasInvalidUsage;

				if (!bUsingCookedEditorData && Entry.bHasAnyError && !Audit.bHasAnyError)
				{
					// Only populate on error for performance/memory reasons
					Audit.AssetName = Private::GetMaterialMeshName(*Component);
					Audit.FallbackMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
				}

				Audit.bHasAnyError |= Entry.bHasAnyError;

#if !(UE_BUILD_SHIPPING) || WITH_EDITOR
				if (!bUsingCookedEditorData && !bNaniteForceEnableMeshes)
				{
					if (Entry.bHasUnsupportedBlendMode && ShouldLogMaterialWarning(Entry.Material->GetName(), Audit.AssetName))
					{
						const FString BlendModeName = GetBlendModeString(Entry.Material->GetBlendMode());
						if (Private::IsMaterialSkeletalMesh(*Component))
						{
							UE_LOGF(
								LogSkeletalMesh, Warning,
								"Invalid material [%ls] used on Nanite skeletal mesh [%ls]. Only opaque or masked blend modes are currently supported, [%ls] blend mode was specified.",
								*Entry.Material->GetName(),
								*Audit.AssetName,
								*BlendModeName
							);
						}
						else
						{
							UE_LOGF(
								LogStaticMesh, Warning,
								"Invalid material [%ls] used on Nanite static mesh [%ls]. Only opaque or masked blend modes are currently supported, [%ls] blend mode was specified. (NOTE: \"Disallow Nanite\" on static mesh components can be used to suppress this warning and forcibly render the object as non-Nanite.)",
								*Entry.Material->GetName(),
								*Audit.AssetName,
								*BlendModeName
							);
						}
					}
					if (Entry.bHasUnsupportedShadingModel && ShouldLogMaterialWarning(Entry.Material->GetName(), Audit.AssetName))
					{
						const FString ShadingModelString = GetShadingModelFieldString(Entry.Material->GetShadingModels());
						if (Private::IsMaterialSkeletalMesh(*Component))
						{
							UE_LOGF(
								LogSkeletalMesh, Warning,
								"Invalid material [%ls] used on Nanite skeletal mesh [%ls]. The SingleLayerWater shading model is currently not supported, [%ls] shading model was specified.",
								*Entry.Material->GetName(),
								*Audit.AssetName,
								*ShadingModelString
							);
						}
						else
						{
							UE_LOGF(
								LogStaticMesh, Warning,
								"Invalid material [%ls] used on Nanite static mesh [%ls]. The SingleLayerWater shading model is currently not supported, [%ls] shading model was specified. (NOTE: \"Disallow Nanite\" on static mesh components can be used to suppress this warning and forcibly render the object as non-Nanite.)",
								*Entry.Material->GetName(),
								*Audit.AssetName,
								*ShadingModelString
							);
						}
					}
				}
#endif
			}
		}

		return Audit;
	}
}