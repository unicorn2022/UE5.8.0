// Copyright Epic Games, Inc. All Rights Reserved.

// SlateIM-based debug overlay summarising every Mass-driven Instanced Skinned Mesh Component (aka ISKMC) in the world.
//
// Toggle with the console command "mh.Crowd.DebugISKMs".

#include "HAL/Platform.h"

#if !UE_BUILD_SHIPPING

#include "Components/InstancedSkinnedMeshComponent.h"
#include "ConvexVolume.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "MassLODTypes.h"
#include "MassRepresentationSubsystem.h"
#include "MassSkinnedMeshRepresentationTypes.h"
#include "Math/BoxSphereBounds.h"
#include "Math/Transform.h"
#include "Math/Vector2D.h"
#include "Mass/MetaHumanMassRepresentationSubsystem.h"
#include "ReferenceSkeleton.h"
#include "SceneView.h"
#include "SlateIM.h"
#include "SlateIMWidgetBase.h"
#include "Styling/AppStyle.h"
#include "Templates/UnrealTemplate.h"
#include "UnrealClient.h"
#include "Widgets/Layout/Anchors.h"

namespace UE::MetaHuman::Crowd::Debug
{
	namespace Private
	{
		// One ISKMC's bucketing key plus per-frame stats. Identity is (Role, BoneCount); the rest
		// is rolled-up data shown in the table.
		struct FBucketKey
		{
			FName Role = NAME_None;
			int32 BoneCount = INDEX_NONE;

			bool operator==(const FBucketKey& Other) const
			{
				return Role == Other.Role && BoneCount == Other.BoneCount;
			}

			friend uint32 GetTypeHash(const FBucketKey& Key)
			{
				return HashCombine(GetTypeHash(Key.Role), GetTypeHash(Key.BoneCount));
			}
		};

		struct FBucketStats
		{
			// Dedup across infos -- two infos sharing an ISKMC must not double-count
			TSet<FObjectKey> UniqueComponents;
			// Dedup of the underlying skinned assets
			TSet<FObjectKey> UniqueMeshes;
			int32 Instances = 0;
			int32 InstancesInFrustum = 0;

			// Custom-data float width per component. Stored as a set so we can detect "mixed"
			// values (i.e. two ISKMCs in the same bucket with different widths -- unusual, but
			// surfaceable rather than silently averaged).
			TSet<int32> CustomDataFloatWidths;

			int32 ISKMCsWithCustomData = 0;
		};

		// Bucket key for the "material slots causing multiple ISKMCs" table. A material slot is
		// owned by a particular mesh: two meshes with the same slot name are tracked separately,
		// because the same FName on different assets is a different visual binding.
		struct FMaterialSlotKey
		{
			FObjectKey MeshAsset;
			FName SlotName = NAME_None;

			bool operator==(const FMaterialSlotKey& Other) const
			{
				return MeshAsset == Other.MeshAsset && SlotName == Other.SlotName;
			}

			friend uint32 GetTypeHash(const FMaterialSlotKey& Key)
			{
				return HashCombine(GetTypeHash(Key.MeshAsset), GetTypeHash(Key.SlotName));
			}
		};

		// Per-(mesh, slot) accumulation. EffectiveMaterials is the set of distinct materials seen
		// across the ISKMCs sharing this mesh -- if it ends up >1, this slot is causing the mesh
		// to fan out across multiple ISKMCs (because differing effective materials per instance
		// can't share a single component). Mesh material is the asset's authored material at the
		// slot (NOT a per-instance / per-component override) and is recorded once.
		struct FMaterialSlotStats
		{
			// Per-slot dedup -- ensures each ISKMC contributes once even if seen via multiple
			// infos or subsystems.
			TSet<FObjectKey> UniqueComponents;
			TSet<TWeakObjectPtr<UMaterialInterface>> EffectiveMaterials;
			int32 Instances = 0;
			TWeakObjectPtr<UMaterialInterface> MeshMaterial;
			FString MeshName;
			FString SlotNameString;
		};

		FName ClassifyRole(UMassRepresentationSubsystem& Subsystem, FSkinnedMeshInstanceVisualizationDescHandle Handle, int32 MeshIndex)
		{
			if (UMetaHumanMassRepresentationSubsystem* MHSubsystem = Cast<UMetaHumanMassRepresentationSubsystem>(&Subsystem))
			{
				const TConstArrayView<FName> Roles = MHSubsystem->GetMeshRolesForDesc(Handle);
				if (Roles.IsValidIndex(MeshIndex))
				{
					return Roles[MeshIndex];
				}
			}
			return TEXT("Other");
		}

		int32 GetBoneCount(USkinnedAsset* Asset)
		{
			if (!Asset)
			{
				return INDEX_NONE;
			}
			return Asset->GetRefSkeleton().GetNum();
		}

		// Build the active player's view frustum as an FConvexVolume. Returns false if no usable
		// view exists (no PIE/standalone client, no player controller, no viewport, etc.).
		bool BuildPlayerViewFrustum(UWorld& World, FConvexVolume& OutFrustum)
		{
			ULocalPlayer* LocalPlayer = nullptr;
			if (UGameInstance* GameInstance = World.GetGameInstance())
			{
				LocalPlayer = GameInstance->GetFirstGamePlayer();
			}
			if (!LocalPlayer || !LocalPlayer->ViewportClient)
			{
				return false;
			}

			FViewport* Viewport = LocalPlayer->ViewportClient->Viewport;
			if (!Viewport)
			{
				return false;
			}

			FSceneViewProjectionData ProjectionData;
			if (!LocalPlayer->GetProjectionData(Viewport, ProjectionData))
			{
				return false;
			}

			GetViewFrustumBounds(OutFrustum, ProjectionData.ComputeViewProjectionMatrix(), /*bUseNearPlane=*/false);
			return true;
		}

		// Count how many of an ISKMC's instances have their world-space bounds intersecting the frustum.
		// AABB test (not sphere) for tighter culling on elongated character meshes.
		int32 CountInstancesInFrustum(UInstancedSkinnedMeshComponent& Component, const FConvexVolume& Frustum)
		{
			USkinnedAsset* Asset = Component.GetSkinnedAsset();
			if (!Asset)
			{
				return 0;
			}

			const FBoxSphereBounds LocalBounds = Asset->GetBounds();
			const int32 InstanceCount = Component.GetInstanceCount();

			int32 InFrustum = 0;
			for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
			{
				const FPrimitiveInstanceId InstanceId = Component.GetInstanceId(InstanceIndex);
				if (!InstanceId.IsValid())
				{
					continue;
				}

				FTransform InstanceWorldTransform;
				if (!Component.GetInstanceTransform(InstanceId, InstanceWorldTransform, /*bWorldSpace=*/true))
				{
					continue;
				}

				const FBoxSphereBounds WorldBounds = LocalBounds.TransformBy(InstanceWorldTransform);
				const FBox WorldBox = WorldBounds.GetBox();
				if (Frustum.IntersectBox(WorldBox.GetCenter(), WorldBox.GetExtent()))
				{
					++InFrustum;
				}
			}

			return InFrustum;
		}

		// Walk all infos in a subsystem and accumulate per-bucket stats. Each ISKMC is added to
		// at most one bucket-stats entry (across all infos that share it) thanks to the per-bucket
		// UniqueComponents set.
		void GatherSubsystemStats(
			UMassRepresentationSubsystem& Subsystem,
			const FConvexVolume* Frustum,
			TMap<FBucketKey, FBucketStats>& InOutBuckets,
			TMap<FMaterialSlotKey, FMaterialSlotStats>& InOutMaterialSlotStats,
			int32& InOutTotalUniqueISKMCs,
			int32& InOutTotalInstances,
			int32& InOutTotalInFrustum,
			int32& InOutTotalISKMCsWithCustomData,
			TSet<FObjectKey>& InOutGloballyVisitedComponents)
		{
			FMassInstancedSkinnedMeshInfoArrayView Infos = Subsystem.GetMutableInstancedSkinnedMeshInfos();

			for (int32 InfoIndex = 0; InfoIndex < Infos.Num(); ++InfoIndex)
			{
				FMassInstancedSkinnedMeshInfo& Info = Infos[InfoIndex];
				const FSkinnedMeshInstanceVisualizationDesc& Desc = Info.GetDesc();

				// SkinnedMeshComponentRefs in the LOD range is index-aligned with Desc.Meshes for
				// the today-case (single LOD range covering all meshes). Probe at High to grab the
				// fully-populated range; if MetaHuman crowd ever introduces per-mesh LOD thresholds
				// we'd need a real range-mesh-index -> desc-mesh-index remap.
				FMassLODInstancedSkinnedMeshSignificanceRange* Range = Info.GetLODSignificanceRange(static_cast<float>(EMassLOD::High));
				if (!Range)
				{
					continue;
				}

				FSkinnedMeshInstanceVisualizationDescHandle Handle;
				// Reverse-engineer the handle from the info's index. UMassRepresentationSubsystem
				// doesn't expose a handle->info accessor, so we use the array index that the
				// FMassInstancedSkinnedMeshInfoArrayView walks in registration order.
				Handle = FSkinnedMeshInstanceVisualizationDescHandle(InfoIndex);

				const int32 MeshCount = FMath::Min(Range->SkinnedMeshComponentRefs.Num(), Desc.Meshes.Num());
				for (int32 MeshIndex = 0; MeshIndex < MeshCount; ++MeshIndex)
				{
					UInstancedSkinnedMeshComponent* Component = Range->SkinnedMeshComponentRefs[MeshIndex].ResolveObjectPtr();
					if (!Component)
					{
						continue;
					}

					const FObjectKey ComponentKey(Component);

					const FName Role = ClassifyRole(Subsystem, Handle, MeshIndex);
					const int32 BoneCount = GetBoneCount(Component->GetSkinnedAsset());

					FBucketKey Key;
					Key.Role = Role;
					Key.BoneCount = BoneCount;

					FBucketStats& Stats = InOutBuckets.FindOrAdd(Key);

					bool bAlreadyInBucket = false;
					Stats.UniqueComponents.Add(ComponentKey, &bAlreadyInBucket);

					// Accumulate per-component contribution only the first time we see this ISKMC
					// in this bucket. A component shared across infos still has one instance count.
					if (!bAlreadyInBucket)
					{
						if (USkinnedAsset* Asset = Component->GetSkinnedAsset())
						{
							Stats.UniqueMeshes.Add(FObjectKey(Asset));
						}

						const int32 ComponentInstanceCount = Component->GetInstanceCount();
						Stats.Instances += ComponentInstanceCount;

						const int32 NumCustomFloats = Component->GetNumCustomDataFloats();
						Stats.CustomDataFloatWidths.Add(NumCustomFloats);
						if (NumCustomFloats > 0)
						{
							++Stats.ISKMCsWithCustomData;
						}

						if (Frustum)
						{
							Stats.InstancesInFrustum += CountInstancesInFrustum(*Component, *Frustum);
						}
					}

					// Globally-unique counts -- a component shared between two subsystems (rare but
					// possible) should still count once toward the world totals.
					bool bAlreadyVisitedGlobally = false;
					InOutGloballyVisitedComponents.Add(ComponentKey, &bAlreadyVisitedGlobally);
					if (!bAlreadyVisitedGlobally)
					{
						++InOutTotalUniqueISKMCs;
						InOutTotalInstances += Component->GetInstanceCount();
						if (Frustum)
						{
							InOutTotalInFrustum += CountInstancesInFrustum(*Component, *Frustum);
						}
						if (Component->GetNumCustomDataFloats() > 0)
						{
							++InOutTotalISKMCsWithCustomData;
						}

						// Per-(mesh, slot) accumulation. Done under the globally-visited gate so
						// each ISKMC contributes exactly once across all subsystems/infos. For each
						// material slot on the component's asset we record the *effective* material
						// (Component->GetMaterial resolves through OverrideMaterials, falling back
						// to the asset's slot material). When a single mesh ends up with multiple
						// effective materials at the same slot across different ISKMCs, that's the
						// signal the slot is causing the mesh to be split across components.
						if (USkinnedAsset* Asset = Component->GetSkinnedAsset())
						{
							const TArray<FSkeletalMaterial>& AssetMaterials = Asset->GetMaterials();
							const int32 ComponentInstanceCount = Component->GetInstanceCount();
							for (int32 SlotIndex = 0; SlotIndex < AssetMaterials.Num(); ++SlotIndex)
							{
								const FSkeletalMaterial& AssetSlot = AssetMaterials[SlotIndex];

								FMaterialSlotKey SlotKey;
								SlotKey.MeshAsset = FObjectKey(Asset);
								SlotKey.SlotName = AssetSlot.MaterialSlotName;

								FMaterialSlotStats& SlotStats = InOutMaterialSlotStats.FindOrAdd(SlotKey);
								if (SlotStats.UniqueComponents.IsEmpty())
								{
									// Record one-time per-slot identity from the asset, not the
									// per-component override, so the table reports the mesh's
									// authored material regardless of which ISKMC we hit first.
									SlotStats.MeshMaterial = AssetSlot.MaterialInterface;
									SlotStats.MeshName = Asset->GetName();
									SlotStats.SlotNameString = AssetSlot.MaterialSlotName.ToString();
								}

								bool bAlreadyInSlot = false;
								SlotStats.UniqueComponents.Add(ComponentKey, &bAlreadyInSlot);
								if (!bAlreadyInSlot)
								{
									SlotStats.Instances += ComponentInstanceCount;
									SlotStats.EffectiveMaterials.Add(Component->GetMaterial(SlotIndex));
								}
							}
						}
					}
				}
			}
		}

		// Returns "4" for a single width, "0-4" for a range, "-" when no widths recorded.
		FString FormatCustomFloatWidth(const TSet<int32>& Widths)
		{
			if (Widths.IsEmpty())
			{
				return TEXT("-");
			}
			if (Widths.Num() == 1)
			{
				return FString::Printf(TEXT("%d"), *Widths.CreateConstIterator());
			}
			int32 MinWidth = TNumericLimits<int32>::Max();
			int32 MaxWidth = TNumericLimits<int32>::Min();
			for (const int32 Width : Widths)
			{
				MinWidth = FMath::Min(MinWidth, Width);
				MaxWidth = FMath::Max(MaxWidth, Width);
			}
			return FString::Printf(TEXT("%d-%d"), MinWidth, MaxWidth);
		}

		// Returns "10" for a single bone count, "10-15" for a range, "-" when unknown.
		FString FormatBoneRange(int32 MinBones, int32 MaxBones)
		{
			if (MinBones == INDEX_NONE && MaxBones == INDEX_NONE)
			{
				return TEXT("-");
			}
			if (MinBones == MaxBones)
			{
				return FString::Printf(TEXT("%d"), MinBones);
			}
			return FString::Printf(TEXT("%d-%d"), MinBones, MaxBones);
		}

		// Cap on table rows per role. When a role's per-bone-count buckets exceed this number,
		// they're merged into this many condensed rows by partitioning the bone-count-sorted list
		// into contiguous slices.
		//
		// This aggregates the data only when necessary to fit it on the screen.
		constexpr int32 MaxRowsPerRole = 8;

		// Cap on rows in the "material slots causing multiple ISKMCs" table. We only show the
		// worst offenders -- anything beyond this is truncated rather than aggregated, since
		// aggregating distinct (mesh, slot) pairs wouldn't yield a meaningful row.
		constexpr int32 MaxMaterialSlotRows = 8;

		// One row in the rendered table. Either a single per-bone-count bucket (when the role
		// fits under MaxRowsPerRole) or a merged slice of consecutive bone-count buckets.
		struct FDisplayRow
		{
			FName Role;
			int32 MinBones = INDEX_NONE;
			int32 MaxBones = INDEX_NONE;
			int32 ISKMCs = 0;
			int32 Meshes = 0;
			int32 Instances = 0;
			int32 InstancesInFrustum = 0;
			TSet<int32> CustomDataFloatWidths;
		};

		// Merge a contiguous slice [SliceStart, SliceEnd) of per-bone-count buckets into one row.
		// All buckets in the slice share a Role.
		FDisplayRow MergeSlice(TConstArrayView<TPair<FBucketKey, FBucketStats*>> SortedRoleBuckets, int32 SliceStart, int32 SliceEnd)
		{
			check(SliceStart < SliceEnd);
			check(SliceEnd <= SortedRoleBuckets.Num());

			FDisplayRow Row;
			Row.Role = SortedRoleBuckets[SliceStart].Key.Role;
			Row.MinBones = SortedRoleBuckets[SliceStart].Key.BoneCount;
			Row.MaxBones = SortedRoleBuckets[SliceEnd - 1].Key.BoneCount;

			TSet<FObjectKey> MergedComponents;
			TSet<FObjectKey> MergedMeshes;
			for (int32 Index = SliceStart; Index < SliceEnd; ++Index)
			{
				const FBucketStats& Stats = *SortedRoleBuckets[Index].Value;
				MergedComponents.Append(Stats.UniqueComponents);
				MergedMeshes.Append(Stats.UniqueMeshes);
				Row.Instances += Stats.Instances;
				Row.InstancesInFrustum += Stats.InstancesInFrustum;
				Row.CustomDataFloatWidths.Append(Stats.CustomDataFloatWidths);
			}
			Row.ISKMCs = MergedComponents.Num();
			Row.Meshes = MergedMeshes.Num();
			return Row;
		}

		// Stable role ordering for display: Face, Body, Groom, Outfit, then anything else
		// alphabetical, with Other (and unrecognised) last.
		int32 RoleSortIndex(FName Role)
		{
			static const TArray<FName> Order = {
				UE::MetaHuman::Crowd::ISKMRole::Face,
				UE::MetaHuman::Crowd::ISKMRole::Body,
				UE::MetaHuman::Crowd::ISKMRole::Groom,
				UE::MetaHuman::Crowd::ISKMRole::Outfit,
			};
			const int32 Index = Order.IndexOfByKey(Role);
			return Index != INDEX_NONE ? Index : Order.Num();
		}
	}

	class FISKMDebugWidget : public FSlateIMWidgetWithCommandBase
	{
	public:
		FISKMDebugWidget()
			: FSlateIMWidgetWithCommandBase(
				TEXT("MetaHumanCrowdISKMDebug"),
				TEXT("mh.Crowd.DebugISKMs"),
				TEXT("Toggle the MetaHuman crowd ISKM debug overlay (aggregates Mass instanced skinned mesh components by role and bone count)."))
		{
			// Anchored at top-left as a point. SConstraintCanvas treats Layout.Size as slate-unit
			// pixels in this anchor mode, so we recompute the height per frame from the viewport
			// size to track resolution changes; width stays at a fixed 800px that fits the table.
			Layout.Anchors = FAnchors(0.0f, 0.0f, 0.0f, 0.0f);
			Layout.Alignment = FVector2f(0.0f, 0.0f);
			Layout.Offset = FVector2f(20.0f, 20.0f);
			Layout.Size = FVector2f(800.0f, 540.0f);
		}

		// Fraction of viewport height the overlay should occupy. Width is kept at a fixed pixel
		// size below since the table's content width doesn't scale with screen height.
		static constexpr float ViewportHeightFraction = 0.8f;

	protected:
		virtual void DrawWidget(float DeltaTime) override;

	private:
		void DrawForWorld(UWorld& World);

		SlateIM::FViewportRootLayout Layout;
	};

	void FISKMDebugWidget::DrawWidget(float DeltaTime)
	{
		if (!SlateIM::CanUpdateSlateIM())
		{
			return;
		}

		UGameViewportClient* ViewportClient = GEngine ? GEngine->GameViewport.Get() : nullptr;
		if (!ViewportClient)
		{
			return;
		}

		UWorld* World = ViewportClient->GetWorld();
		if (!World)
		{
			return;
		}

		// Resize the overlay to ViewportHeightFraction of the current viewport height. Done per
		// frame so window resizing during PIE updates immediately. GetViewportSize returns the
		// rendering viewport rect in slate units, which is what SConstraintCanvas wants.
		FVector2D ViewportSize = FVector2D::ZeroVector;
		ViewportClient->GetViewportSize(ViewportSize);
		if (ViewportSize.Y > 0.0)
		{
			Layout.Size = FVector2f(Layout.Size.GetValue().X, static_cast<float>(ViewportSize.Y) * ViewportHeightFraction);
		}

		if (SlateIM::BeginViewportRoot(TEXT("MetaHumanCrowdISKMDebug"), ViewportClient, {.Layout = Layout}))
		{
			// Fill both axes so the border (the dark-grey panel) takes the whole root rect, and the
			// vertical stack inside it fills horizontally so its child Text() widgets don't shrink
			// to their narrowest wrapped width.
			SlateIM::HAlign(HAlign_Fill);
			SlateIM::VAlign(VAlign_Fill);
			SlateIM::Fill();
			SlateIM::BeginBorder(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")), Orient_Vertical, /*bAbsorbMouse=*/true);

			SlateIM::HAlign(HAlign_Fill);
			SlateIM::VAlign(VAlign_Fill);
			SlateIM::Fill();
			SlateIM::BeginVerticalStack();

			DrawForWorld(*World);

			SlateIM::EndVerticalStack();
			SlateIM::EndBorder();
		}
		SlateIM::EndRoot();
	}

	void FISKMDebugWidget::DrawForWorld(UWorld& World)
	{
		using namespace Private;

		const TArray<UMassRepresentationSubsystem*> Subsystems = World.GetSubsystemArrayCopy<UMassRepresentationSubsystem>();

		FConvexVolume PlayerFrustum;
		const bool bHaveFrustum = BuildPlayerViewFrustum(World, PlayerFrustum);
		const FConvexVolume* FrustumPtr = bHaveFrustum ? &PlayerFrustum : nullptr;

		// Walk every subsystem once. Bucket stats are subsystem-agnostic by design -- shared
		// ISKMCs across subsystems are deduped via GloballyVisitedComponents on the totals side.
		TMap<FBucketKey, FBucketStats> Buckets;
		TMap<FMaterialSlotKey, FMaterialSlotStats> MaterialSlotStats;
		int32 TotalAppearances = 0;
		int32 TotalUniqueISKMCs = 0;
		int32 TotalInstances = 0;
		int32 TotalInFrustum = 0;
		int32 TotalISKMCsWithCustomData = 0;
		TSet<FObjectKey> GloballyVisitedComponents;

		for (UMassRepresentationSubsystem* Subsystem : Subsystems)
		{
			if (!Subsystem)
			{
				continue;
			}

			// Appearance count is MetaHuman-specific; the base subsystem doesn't track them.
			if (UMetaHumanMassRepresentationSubsystem* MHSubsystem = Cast<UMetaHumanMassRepresentationSubsystem>(Subsystem))
			{
				FMassInstancedSkinnedMeshInfoArrayView Infos = MHSubsystem->GetMutableInstancedSkinnedMeshInfos();
				TotalAppearances += Infos.Num();
			}

			GatherSubsystemStats(*Subsystem, FrustumPtr, Buckets, MaterialSlotStats, TotalUniqueISKMCs, TotalInstances, TotalInFrustum, TotalISKMCsWithCustomData, GloballyVisitedComponents);
		}

		// Top totals block. HAlign(Fill) before every Text so its slot in the vertical stack takes
		// the full width -- without it the stack child slot auto-sizes to the text's wrapped-narrow
		// desired width (SlateIM::Text uses AutoWrapText).
		auto FillRowText = [](const FString& InText)
		{
			SlateIM::HAlign(HAlign_Fill);
			SlateIM::Text(*InText);
		};

		FillRowText(FString::Printf(TEXT("World: %s    Frustum: %s"),
			*World.GetName(),
			bHaveFrustum ? TEXT("ok") : TEXT("(no player view)")));
		FillRowText(FString::Printf(TEXT("Appearances:     %d"), TotalAppearances));
		FillRowText(FString::Printf(TEXT("Unique ISKMCs:   %d"), TotalUniqueISKMCs));
		FillRowText(FString::Printf(TEXT("Total instances: %d  (in frustum: %d)"), TotalInstances, TotalInFrustum));
		FillRowText(FString::Printf(TEXT("Custom floats:   active on %d / %d ISKMCs"), TotalISKMCsWithCustomData, TotalUniqueISKMCs));

		if (Buckets.IsEmpty())
		{
			SlateIM::Text(TEXT("(No instanced skinned meshes registered)"));
			return;
		}

		// Sort buckets: known roles in canonical order, then unknown
		// roles alphabetically. Within a role, by ascending bone count.
		TArray<TPair<FBucketKey, FBucketStats*>> SortedBuckets;
		SortedBuckets.Reserve(Buckets.Num());
		for (TPair<FBucketKey, FBucketStats>& Pair : Buckets)
		{
			SortedBuckets.Emplace(Pair.Key, &Pair.Value);
		}
		SortedBuckets.Sort([](const TPair<FBucketKey, FBucketStats*>& A, const TPair<FBucketKey, FBucketStats*>& B)
			{
				const int32 RoleA = RoleSortIndex(A.Key.Role);
				const int32 RoleB = RoleSortIndex(B.Key.Role);
				if (RoleA != RoleB)
				{
					return RoleA < RoleB;
				}
				if (A.Key.Role != B.Key.Role)
				{
					return A.Key.Role.LexicalLess(B.Key.Role);
				}
				return A.Key.BoneCount < B.Key.BoneCount;
			});

		// Build display rows per role. For roles with <= MaxRowsPerRole buckets, each bucket gets
		// its own row. For roles with more, partition the role's bone-count-sorted slice into
		// MaxRowsPerRole contiguous chunks using i*N/K bounds (distributes leftovers fairly: the
		// first (N mod K) chunks are one wider than the rest, no integer truncation losses).
		TArray<FDisplayRow> Rows;
		Rows.Reserve(SortedBuckets.Num());

		for (int32 RoleStart = 0; RoleStart < SortedBuckets.Num();)
		{
			const FName Role = SortedBuckets[RoleStart].Key.Role;
			int32 RoleEnd = RoleStart + 1;
			while (RoleEnd < SortedBuckets.Num() && SortedBuckets[RoleEnd].Key.Role == Role)
			{
				++RoleEnd;
			}

			const int32 RoleBucketCount = RoleEnd - RoleStart;
			if (RoleBucketCount <= MaxRowsPerRole)
			{
				for (int32 Index = RoleStart; Index < RoleEnd; ++Index)
				{
					Rows.Add(MergeSlice(SortedBuckets, Index, Index + 1));
				}
			}
			else
			{
				const TConstArrayView<TPair<FBucketKey, FBucketStats*>> RoleSlice(&SortedBuckets[RoleStart], RoleBucketCount);
				for (int32 ChunkIndex = 0; ChunkIndex < MaxRowsPerRole; ++ChunkIndex)
				{
					const int32 ChunkStart = (ChunkIndex * RoleBucketCount) / MaxRowsPerRole;
					const int32 ChunkEnd = ((ChunkIndex + 1) * RoleBucketCount) / MaxRowsPerRole;
					if (ChunkStart == ChunkEnd)
					{
						// Defensive: if MaxRowsPerRole > RoleBucketCount we've handled it above.
						// This branch can't be hit, but guard so partition math never produces empty slices.
						continue;
					}
					Rows.Add(MergeSlice(RoleSlice, ChunkStart, ChunkEnd));
				}
			}

			RoleStart = RoleEnd;
		}

		// Bucket table.
		SlateIM::BeginScrollBox(Orient_Vertical);
		SlateIM::BeginTable();

		SlateIM::BeginTableHeader();
		SlateIM::InitialTableColumnWidth(80.f);
		SlateIM::AddTableColumn(TEXT("Role"), TEXT("Role"));
		SlateIM::InitialTableColumnWidth(70.f);
		SlateIM::AddTableColumn(TEXT("Bones"), TEXT("Bones"));
		SlateIM::InitialTableColumnWidth(80.f);
		SlateIM::AddTableColumn(TEXT("ISKMCs"), TEXT("ISKMCs"));
		SlateIM::InitialTableColumnWidth(80.f);
		SlateIM::AddTableColumn(TEXT("Meshes"), TEXT("Meshes"));
		SlateIM::InitialTableColumnWidth(90.f);
		SlateIM::AddTableColumn(TEXT("Instances"), TEXT("Instances"));
		SlateIM::InitialTableColumnWidth(90.f);
		SlateIM::AddTableColumn(TEXT("InFrustum"), TEXT("In frustum"));
		SlateIM::InitialTableColumnWidth(140.f);
		SlateIM::AddTableColumn(TEXT("CustomFloats"), TEXT("Custom floats"));
		SlateIM::EndTableHeader();

		SlateIM::BeginTableBody();

		for (const FDisplayRow& Row : Rows)
		{
			SlateIM::NextTableCell();
			SlateIM::Text(*Row.Role.ToString());

			SlateIM::NextTableCell();
			SlateIM::Text(*FormatBoneRange(Row.MinBones, Row.MaxBones));

			SlateIM::NextTableCell();
			SlateIM::Text(*FString::Printf(TEXT("%d"), Row.ISKMCs));

			SlateIM::NextTableCell();
			SlateIM::Text(*FString::Printf(TEXT("%d"), Row.Meshes));

			SlateIM::NextTableCell();
			SlateIM::Text(*FString::Printf(TEXT("%d"), Row.Instances));

			SlateIM::NextTableCell();
			if (FrustumPtr)
			{
				SlateIM::Text(*FString::Printf(TEXT("%d"), Row.InstancesInFrustum));
			}
			else
			{
				SlateIM::Text(TEXT("-"));
			}

			SlateIM::NextTableCell();
			SlateIM::Text(*FormatCustomFloatWidth(Row.CustomDataFloatWidths));
		}

		SlateIM::EndTableBody();
		SlateIM::EndTable();
		SlateIM::EndScrollBox();

		// "Top material slots causing multiple ISKMCs" table.
		//
		// A slot causes splitting when the same (mesh, slot) pair has more than one effective
		// material across the ISKMCs sharing that mesh -- per-instance overrides at that slot
		// can't be packed into one component, so the runtime fans out into multiple ISKMCs.
		// Sort offenders by ISKMC count desc (more split = worse), tie-break by instance count
		// desc, and truncate to MaxMaterialSlotRows.
		struct FMaterialSlotRow
		{
			FString SlotName;
			FString MeshMaterialName;
			FString MeshName;
			int32 ISKMCs = 0;
			int32 Instances = 0;
		};

		TArray<FMaterialSlotRow> MaterialSlotRows;
		MaterialSlotRows.Reserve(MaterialSlotStats.Num());
		for (const TPair<FMaterialSlotKey, FMaterialSlotStats>& Pair : MaterialSlotStats)
		{
			const FMaterialSlotStats& Stats = Pair.Value;
			if (Stats.EffectiveMaterials.Num() <= 1)
			{
				// Not split -- every ISKMC sharing this mesh resolved the same effective material
				// at this slot, so the slot didn't force a fan-out. Skip.
				continue;
			}

			FMaterialSlotRow& Row = MaterialSlotRows.AddDefaulted_GetRef();
			Row.SlotName = Stats.SlotNameString;
			Row.MeshName = Stats.MeshName;
			if (UMaterialInterface* MeshMaterial = Stats.MeshMaterial.Get())
			{
				Row.MeshMaterialName = MeshMaterial->GetName();
			}
			else
			{
				Row.MeshMaterialName = TEXT("(none)");
			}
			Row.ISKMCs = Stats.UniqueComponents.Num();
			Row.Instances = Stats.Instances;
		}

		MaterialSlotRows.Sort([](const FMaterialSlotRow& A, const FMaterialSlotRow& B)
			{
				if (A.ISKMCs != B.ISKMCs)
				{
					return A.ISKMCs > B.ISKMCs;
				}
				if (A.Instances != B.Instances)
				{
					return A.Instances > B.Instances;
				}
				// Stable, deterministic ordering for equal counts so the table doesn't shuffle
				// frame-to-frame on ties.
				if (A.MeshName != B.MeshName)
				{
					return A.MeshName < B.MeshName;
				}
				return A.SlotName < B.SlotName;
			});

		if (MaterialSlotRows.Num() > MaxMaterialSlotRows)
		{
			MaterialSlotRows.SetNum(MaxMaterialSlotRows);
		}

		FillRowText(TEXT(""));
		FillRowText(FString::Printf(TEXT("Top %d material slots causing multiple ISKMCs"), MaterialSlotRows.Num()));

		if (MaterialSlotRows.IsEmpty())
		{
			SlateIM::Text(TEXT("(No material slots are forcing multiple ISKMCs)"));
		}
		else
		{
			SlateIM::BeginScrollBox(Orient_Vertical);
			SlateIM::BeginTable();

			SlateIM::BeginTableHeader();
			SlateIM::InitialTableColumnWidth(160.f);
			SlateIM::AddTableColumn(TEXT("Slot"), TEXT("Slot"));
			SlateIM::InitialTableColumnWidth(180.f);
			SlateIM::AddTableColumn(TEXT("MeshMaterial"), TEXT("Mesh material"));
			SlateIM::InitialTableColumnWidth(180.f);
			SlateIM::AddTableColumn(TEXT("Mesh"), TEXT("Mesh"));
			SlateIM::InitialTableColumnWidth(80.f);
			SlateIM::AddTableColumn(TEXT("ISKMCs"), TEXT("ISKMCs"));
			SlateIM::InitialTableColumnWidth(90.f);
			SlateIM::AddTableColumn(TEXT("Instances"), TEXT("Instances"));
			SlateIM::EndTableHeader();

			SlateIM::BeginTableBody();

			for (const FMaterialSlotRow& Row : MaterialSlotRows)
			{
				SlateIM::NextTableCell();
				SlateIM::Text(*Row.SlotName);

				SlateIM::NextTableCell();
				SlateIM::Text(*Row.MeshMaterialName);

				SlateIM::NextTableCell();
				SlateIM::Text(*Row.MeshName);

				SlateIM::NextTableCell();
				SlateIM::Text(*FString::Printf(TEXT("%d"), Row.ISKMCs));

				SlateIM::NextTableCell();
				SlateIM::Text(*FString::Printf(TEXT("%d"), Row.Instances));
			}

			SlateIM::EndTableBody();
			SlateIM::EndTable();
			SlateIM::EndScrollBox();
		}
	}

	// Static instance owns the console-command registration for the lifetime of the module.
	static FISKMDebugWidget GISKMDebugWidget;
}

#endif // !UE_BUILD_SHIPPING
