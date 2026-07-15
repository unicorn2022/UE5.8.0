// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCrowdBodyMerge.h"

#include "MetaHumanCrowdEditorUtilities.h"
#include "MetaHumanCrowdTypes.h"
#include "MetaHumanGeometryRemoval.h"

#include "Animation/Skeleton.h"
#include "HAL/ConsoleManager.h"
#include "ImageUtils.h"
#include "Logging/StructuredLog.h"
#include "MeshDescription.h"
#include "Misc/Paths.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "SkeletalMeshAttributes.h"
#include "SkeletalMeshOperations.h"
#include "StaticMeshOperations.h"

DEFINE_LOG_CATEGORY_STATIC(LogCrowdBodyMerge, Log, All);

namespace UE::MetaHuman::CrowdBodyMerge
{
	// The resolution of the slot ownership bitmap in pixels
	static constexpr int32 OwnershipBitmapSize = 1024;
	
	// The threshold at which a bone weight is considered significant enough to say that this 
	// vertex is skinned to this bone. 
	//
	// The algorithm looks for stronger weights than this to avoid noisy, unstable results.
	static constexpr float BoneWeightClaimThreshold = 0.05f;

	static TAutoConsoleVariable<bool> CVarDebugBodyMergeSlotOwnership(
		TEXT("metahuman.crowd.DebugBodyMergeSlotOwnership"),
		false,
		TEXT("If true, writes a debug PNG of the slot ownership bitmap to the project's Saved folder and logs more detailed info."),
		ECVF_Default);

	// -------------------------------------------------------------------------
	// Phase 1: Build Slot Ownership Bitmap
	// -------------------------------------------------------------------------

	static void BuildSlotOwnershipBitmap(
		TArray<uint8>& OutBitmap,
		const TArray<FSlotData>& Slots)
	{
		OutBitmap.SetNumUninitialized(OwnershipBitmapSize * OwnershipBitmapSize);
		FMemory::Memset(OutBitmap.GetData(), UnclaimedSlotIndex, OutBitmap.Num());

		for (int32 SlotIdx = 0; SlotIdx < Slots.Num(); ++SlotIdx)
		{
			const FSlotData& Slot = Slots[SlotIdx];
			for (const FSlotItemData& Item : Slot.Items)
			{
				if (!Item.bHasValidFaceMap)
				{
					continue;
				}

				const FImage& FaceMapImage = Item.BodyHiddenFaceMapImage.Image;
				const GeometryRemoval::FHiddenFaceMapSettings& Settings = Item.BodyHiddenFaceMapImage.Settings;
				const int32 FaceMapWidth = FaceMapImage.SizeX;
				const int32 FaceMapHeight = FaceMapImage.SizeY;

				if (FaceMapWidth <= 0 || FaceMapHeight <= 0)
				{
					continue;
				}

				for (int32 FaceMapY = 0; FaceMapY < FaceMapHeight; ++FaceMapY)
				{
					for (int32 FaceMapX = 0; FaceMapX < FaceMapWidth; ++FaceMapX)
					{
						const FLinearColor PixelColor = FaceMapImage.GetOnePixelLinear(FaceMapX, FaceMapY);
						const float PixelValue = FMath::Max3(PixelColor.R, PixelColor.G, PixelColor.B);

						// Pixel affects geometry if it would cause hidden or shrunk behavior
						if (PixelValue < Settings.MinKeepValue)
						{
							const int32 BitmapX = FMath::Clamp(FaceMapX * OwnershipBitmapSize / FaceMapWidth, 0, OwnershipBitmapSize - 1);
							const int32 BitmapY = FMath::Clamp(FaceMapY * OwnershipBitmapSize / FaceMapHeight, 0, OwnershipBitmapSize - 1);
							const int32 BitmapIndex = BitmapY * OwnershipBitmapSize + BitmapX;

							// The first slot to claim a pixel in the bitmap gets to keep it.
							//
							// This helps to keep the results stable when items are added or removed from the collection.
							if (OutBitmap[BitmapIndex] == UnclaimedSlotIndex)
							{
								OutBitmap[BitmapIndex] = static_cast<uint8>(SlotIdx);
							}
						}
					}
				}
			}
		}
	}

	// -------------------------------------------------------------------------
	// Phase 2: Assign vertices to slots, build per-slot max bone weights
	// -------------------------------------------------------------------------

	/** Wraps a UV coordinate to the [0, 1) range using fmod. */
	static float WrapUVCoordinate(float Value)
	{
		float Wrapped = FMath::Fmod(Value, 1.0f);
		if (Wrapped < 0.0f)
		{
			Wrapped += 1.0f;
		}
		return Wrapped;
	}

	/** Looks up the slot index for a UV coordinate in the ownership bitmap. */
	static uint8 LookupSlotForUV(const TArray<uint8>& Bitmap, float U, float V)
	{
		const float WrappedU = WrapUVCoordinate(U);
		const float WrappedV = WrapUVCoordinate(V);
		const int32 BitmapX = FMath::Clamp(FMath::FloorToInt32(WrappedU * OwnershipBitmapSize), 0, OwnershipBitmapSize - 1);
		const int32 BitmapY = FMath::Clamp(FMath::FloorToInt32(WrappedV * OwnershipBitmapSize), 0, OwnershipBitmapSize - 1);
		return Bitmap[BitmapY * OwnershipBitmapSize + BitmapX];
	}

	static void AssignVerticesToSlots(
		const TArray<uint8>& Bitmap,
		TArray<FSlotData>& Slots,
		const FMeshDescription& BodyMeshDesc,
		const FReferenceSkeleton& BodyRefSkeleton,
		TArray<int32>& OutUnclaimedVerts)
	{
		FSkeletalMeshConstAttributes Attrs(BodyMeshDesc);
		if (!Attrs.HasBones())
		{
			return;
		}

		FSkinWeightsVertexAttributesConstRef SkinWeights = Attrs.GetVertexSkinWeights();
		FSkeletalMeshAttributes::FBoneNameAttributesConstRef BoneNames = Attrs.GetBoneNames();

		if (!SkinWeights.IsValid() || !BoneNames.IsValid())
		{
			return;
		}

		// Build bone name array indexed by MeshDescription bone ID
		TArray<FName> BoneNameArray;
		for (const FBoneID BoneID : Attrs.Bones().GetElementIDs())
		{
			if (BoneID.GetValue() >= BoneNameArray.Num())
			{
				BoneNameArray.SetNum(BoneID.GetValue() + 1);
			}
			BoneNameArray[BoneID.GetValue()] = BoneNames.Get(BoneID);
		}

		// Read UV0 from vertex instances -- we need per-vertex UVs for bitmap lookup
		TVertexInstanceAttributesConstRef<FVector2f> VertexInstanceUVs =
			BodyMeshDesc.VertexInstanceAttributes().GetAttributesRef<FVector2f>(MeshAttribute::VertexInstance::TextureCoordinate);

		// For each vertex, find the first vertex instance to get its UV
		int32 VertexCompactIndex = 0;
		for (const FVertexID VertexID : BodyMeshDesc.Vertices().GetElementIDs())
		{
			// Get UV from the first vertex instance of this vertex
			FVector2f UV(0.0f, 0.0f);
			TArrayView<const FVertexInstanceID> VertexInstances = BodyMeshDesc.GetVertexVertexInstanceIDs(VertexID);
			if (VertexInstances.Num() > 0)
			{
				UV = VertexInstanceUVs[VertexInstances[0]];
			}

			const uint8 SlotIndex = LookupSlotForUV(Bitmap, UV.X, UV.Y);

			if (SlotIndex != UnclaimedSlotIndex)
			{
				// This vert is in a claimed area, so we update the slot's MaxBoneWeights map
				// to keep track of which bones are covered by this slot.
				//
				// This info is used to assign slots to verts that are in unclaimed areas, 
				// based on the bones they're skinned to.
				//
				// For example, the hand verts are usually unclaimed by any slot because no 
				// hidden face map covers them. The hand verts are skinned to hand bones that 
				// are parented to the arm bones, which are claimed by e.g. the Top Garment 
				// slot, so the hand geometry will be correctly assigned to the neighboring Top
				// Garment slot.

				FSlotData& Slot = Slots[SlotIndex];

				FVertexBoneWeightsConst Weights = SkinWeights.Get(VertexID);
				for (int32 Idx = 0; Idx < Weights.Num(); ++Idx)
				{
					const float Weight = Weights[Idx].GetWeight();
					if (Weight <= 0.0f)
					{
						break;
					}

					const FBoneIndexType BoneIdx = Weights[Idx].GetBoneIndex();
					if (BoneIdx < static_cast<FBoneIndexType>(BoneNameArray.Num()))
					{
						const FName BoneName = BoneNameArray[BoneIdx];
						float& MaxWeight = Slot.MaxBoneWeights.FindOrAdd(BoneName, 0.0f);
						MaxWeight = FMath::Max(MaxWeight, Weight);
					}
				}
			}
			else
			{
				OutUnclaimedVerts.Add(VertexCompactIndex);
			}

			++VertexCompactIndex;
		}
	}

	// -------------------------------------------------------------------------
	// Phase 3: Resolve unclaimed vertices via bone hierarchy
	// -------------------------------------------------------------------------

	/** Find a slot that claims a bone (or its ancestors) above the threshold.
	 *  When multiple slots claim the same bone, the one with the highest weight wins. */
	static int32 FindSlotByBoneHierarchy(
		const FReferenceSkeleton& RefSkeleton,
		int32 StartBoneIndex,
		const TArray<FSlotData>& Slots,
		bool bDebuggingSlotOwnership = false)
	{
		const FName StartBoneName = RefSkeleton.GetBoneName(StartBoneIndex);

		// Helper: find the slot with the highest weight for a given bone name.
		// Returns the slot index, or INDEX_NONE if no slot claims this bone above threshold.
		auto FindBestSlotForBone = [&Slots](const FName& BoneName, float& OutWeight) -> int32
		{
			int32 BestSlot = INDEX_NONE;
			float BestWeight = 0.0f;
			for (int32 SlotIdx = 0; SlotIdx < Slots.Num(); ++SlotIdx)
			{
				const float* FoundWeight = Slots[SlotIdx].MaxBoneWeights.Find(BoneName);
				if (FoundWeight && *FoundWeight > BoneWeightClaimThreshold && *FoundWeight > BestWeight)
				{
					BestWeight = *FoundWeight;
					BestSlot = SlotIdx;
				}
			}
			OutWeight = BestWeight;
			return BestSlot;
		};

		auto FindSlotName = [&Slots](int32 SlotIndex) -> const FName
		{
			if (SlotIndex >= 0 && SlotIndex < Slots.Num())
			{
				return Slots[SlotIndex].VirtualSlotName;
			}

			return NAME_None;
		};

		// First: check the start bone itself.
		{
			float Weight = 0.0f;
			const int32 FoundSlot = FindBestSlotForBone(StartBoneName, Weight);
			if (FoundSlot != INDEX_NONE)
			{
				if (bDebuggingSlotOwnership)
				{
				UE_LOGFMT(LogCrowdBodyMerge, Log, "CrowdBodyMerge: Bone '{StartBone}' -> self found in slot '{Slot}' (weight {Weight})",
					StartBoneName, FindSlotName(FoundSlot), Weight);
				}
				return FoundSlot;
			}
		}

		// Second: walk UP the hierarchy (parent, grandparent)
		{
			int32 CurrentBone = RefSkeleton.GetParentIndex(StartBoneIndex);
			int32 Depth = 0;

			while (CurrentBone != INDEX_NONE)
			{
				++Depth;
				const FName BoneName = RefSkeleton.GetBoneName(CurrentBone);

				float Weight = 0.0f;
				const int32 FoundSlot = FindBestSlotForBone(BoneName, Weight);
				if (FoundSlot != INDEX_NONE)
				{
					if (bDebuggingSlotOwnership)
					{
					UE_LOGFMT(LogCrowdBodyMerge, Log, "CrowdBodyMerge: Bone '{StartBone}' -> parent walk depth {Depth} -> bone '{Bone}' found in slot '{Slot}' (weight {Weight})",
						StartBoneName, Depth, BoneName, FindSlotName(FoundSlot), Weight);
					}
					return FoundSlot;
				}

				CurrentBone = RefSkeleton.GetParentIndex(CurrentBone);
			}
		}

		if (bDebuggingSlotOwnership)
		{
		UE_LOGFMT(LogCrowdBodyMerge, Warning, "CrowdBodyMerge: Bone '{Bone}' -> no slot found via self or parent walk",
			StartBoneName);
		}

		return INDEX_NONE;
	}

	static void ResolveUnclaimedVerts(
		TArray<uint8>& Bitmap,
		const TArray<int32>& UnclaimedVerts,
		const TArray<FSlotData>& Slots,
		const FMeshDescription& BodyMeshDesc,
		const FReferenceSkeleton& RefSkeleton)
	{
		FSkeletalMeshConstAttributes Attrs(BodyMeshDesc);
		if (!Attrs.HasBones())
		{
			return;
		}

		FSkinWeightsVertexAttributesConstRef SkinWeights = Attrs.GetVertexSkinWeights();
		FSkeletalMeshAttributes::FBoneNameAttributesConstRef BoneNames = Attrs.GetBoneNames();

		if (!SkinWeights.IsValid() || !BoneNames.IsValid())
		{
			return;
		}

		// Build compact-index-to-VertexID and bone name arrays
		TArray<FVertexID> IndexToVertexID;
		IndexToVertexID.Reserve(BodyMeshDesc.Vertices().Num());
		for (const FVertexID VID : BodyMeshDesc.Vertices().GetElementIDs())
		{
			IndexToVertexID.Add(VID);
		}

		TArray<FName> BoneNameArray;
		for (const FBoneID BoneID : Attrs.Bones().GetElementIDs())
		{
			if (BoneID.GetValue() >= BoneNameArray.Num())
			{
				BoneNameArray.SetNum(BoneID.GetValue() + 1);
			}
			BoneNameArray[BoneID.GetValue()] = BoneNames.Get(BoneID);
		}

		TVertexInstanceAttributesConstRef<FVector2f> VertexInstanceUVs =
			BodyMeshDesc.VertexInstanceAttributes().GetAttributesRef<FVector2f>(MeshAttribute::VertexInstance::TextureCoordinate);

		const bool bDebuggingSlotOwnership = CVarDebugBodyMergeSlotOwnership.GetValueOnAnyThread();

		UE_LOGFMT(LogCrowdBodyMerge, Log, "CrowdBodyMerge: Phase 3 - Resolving {NumVerts} unclaimed verts across {NumSlots} slots",
			UnclaimedVerts.Num(), Slots.Num());

		if (bDebuggingSlotOwnership)
		{
			for (int32 SlotIdx = 0; SlotIdx < Slots.Num(); ++SlotIdx)
			{
				const FSlotData& Slot = Slots[SlotIdx];
				FString BoneListStr;
				for (const TPair<FName, float>& BoneWeight : Slot.MaxBoneWeights)
				{
					if (BoneWeight.Value > BoneWeightClaimThreshold)
					{
						if (!BoneListStr.IsEmpty())
						{
							BoneListStr += TEXT(", ");
						}
						BoneListStr += FString::Printf(TEXT("%s=%.4f"), *BoneWeight.Key.ToString(), BoneWeight.Value);
					}
				}
				UE_LOGFMT(LogCrowdBodyMerge, Log, "CrowdBodyMerge: Phase 3 - Slot '{Slot}' (index {Index}): {NumBones} bones in MaxBoneWeights [{BoneList}]",
					Slot.VirtualSlotName, SlotIdx, Slot.MaxBoneWeights.Num(), BoneListStr);
			}
		}

		// Track how many verts end up in each slot for summary
		TMap<uint8, int32> SlotAssignmentCounts;
		int32 FallbackCount = 0;

		for (int32 CompactVertIndex : UnclaimedVerts)
		{
			if (!IndexToVertexID.IsValidIndex(CompactVertIndex))
			{
				continue;
			}

			const FVertexID VertexID = IndexToVertexID[CompactVertIndex];

			// Find the bone influence with the highest weight
			FVertexBoneWeightsConst Weights = SkinWeights.Get(VertexID);
			FBoneIndexType BestBoneIdx = 0;
			float BestWeight = 0.0f;

			for (int32 Idx = 0; Idx < Weights.Num(); ++Idx)
			{
				if (Weights[Idx].GetWeight() > BestWeight)
				{
					BestWeight = Weights[Idx].GetWeight();
					BestBoneIdx = Weights[Idx].GetBoneIndex();
				}
			}

			// Map MeshDescription bone index to RefSkeleton bone index via bone name
			int32 SkeletonBoneIndex = INDEX_NONE;
			if (BestBoneIdx < static_cast<FBoneIndexType>(BoneNameArray.Num()))
			{
				const FName BoneName = BoneNameArray[BestBoneIdx];
				SkeletonBoneIndex = RefSkeleton.FindBoneIndex(BoneName);
			}

			uint8 AssignedSlot = 0;
			bool bUsedFallback = true;

			// Walk the bone hierarchy from the selected bone to find which slot should own this vertex
			if (SkeletonBoneIndex != INDEX_NONE)
			{
				const FName BoneName = RefSkeleton.GetBoneName(SkeletonBoneIndex);
				const int32 FoundSlot = FindSlotByBoneHierarchy(RefSkeleton, SkeletonBoneIndex, Slots, bDebuggingSlotOwnership);
				if (FoundSlot != INDEX_NONE)
				{
					AssignedSlot = static_cast<uint8>(FoundSlot);
					bUsedFallback = false;
				}
				else
				{
					UE_LOGFMT(LogCrowdBodyMerge, Warning, "CrowdBodyMerge: Phase 3 - Vert {Vert}: bone '{Bone}' - no slot found via hierarchy walk. Fallback to first slot.",
						CompactVertIndex, BoneName);
				}
			}
			else
			{
			UE_LOGFMT(LogCrowdBodyMerge, Warning, "CrowdBodyMerge: Phase 3 - Vert {Vert}: no valid bone index. Fallback to first slot.",
				CompactVertIndex);
			}

			if (bUsedFallback)
			{
				++FallbackCount;
			}
			SlotAssignmentCounts.FindOrAdd(AssignedSlot, 0)++;

			// Update the bitmap for this vertex's UV
			FVector2f UV(0.0f, 0.0f);
			TArrayView<const FVertexInstanceID> VertexInstances = BodyMeshDesc.GetVertexVertexInstanceIDs(VertexID);
			if (VertexInstances.Num() > 0)
			{
				UV = VertexInstanceUVs[VertexInstances[0]];
			}
			const float WrappedU = WrapUVCoordinate(UV.X);
			const float WrappedV = WrapUVCoordinate(UV.Y);
			const int32 BitmapX = FMath::Clamp(FMath::FloorToInt32(WrappedU * OwnershipBitmapSize), 0, OwnershipBitmapSize - 1);
			const int32 BitmapY = FMath::Clamp(FMath::FloorToInt32(WrappedV * OwnershipBitmapSize), 0, OwnershipBitmapSize - 1);
			const int32 BitmapIndex = BitmapY * OwnershipBitmapSize + BitmapX;

			if (Bitmap[BitmapIndex] == UnclaimedSlotIndex)
			{
				Bitmap[BitmapIndex] = AssignedSlot;
			}
		}

		// Summary
		for (const TPair<uint8, int32>& Pair : SlotAssignmentCounts)
		{
			const FName& SlotName = (Pair.Key < Slots.Num()) ? Slots[Pair.Key].VirtualSlotName : NAME_None;
			UE_LOGFMT(LogCrowdBodyMerge, Log, "CrowdBodyMerge: Phase 3 summary - Slot '{Slot}' (index {Index}): {NumVerts} verts assigned",
				SlotName, Pair.Key, Pair.Value);
		}
		UE_LOGFMT(LogCrowdBodyMerge, Log, "CrowdBodyMerge: Phase 3 summary - {NumVerts} verts used fallback (first slot)",
			FallbackCount);
	}

	// -------------------------------------------------------------------------
	// Phase 4: Jump Flood Algorithm to fill remaining unclaimed pixels
	// -------------------------------------------------------------------------

	struct FJFASeed
	{
		int16 X = -1;
		int16 Y = -1;
	};

	static void FloodFillBitmap(TArray<uint8>& Bitmap)
	{
		const int32 TotalPixels = OwnershipBitmapSize * OwnershipBitmapSize;

		// Initialize seed buffer from claimed pixels
		TArray<FJFASeed> Seeds;
		Seeds.SetNumUninitialized(TotalPixels);

		for (int32 Y = 0; Y < OwnershipBitmapSize; ++Y)
		{
			for (int32 X = 0; X < OwnershipBitmapSize; ++X)
			{
				const int32 Index = Y * OwnershipBitmapSize + X;
				if (Bitmap[Index] != UnclaimedSlotIndex)
				{
					Seeds[Index].X = static_cast<int16>(X);
					Seeds[Index].Y = static_cast<int16>(Y);
				}
				else
				{
					Seeds[Index].X = -1;
					Seeds[Index].Y = -1;
				}
			}
		}

		TArray<FJFASeed> TempSeeds;
		TempSeeds.SetNumUninitialized(TotalPixels);

		// JFA passes with decreasing step sizes
		for (int32 StepSize = OwnershipBitmapSize / 2; StepSize >= 1; StepSize /= 2)
		{
			FMemory::Memcpy(TempSeeds.GetData(), Seeds.GetData(), TotalPixels * sizeof(FJFASeed));

			for (int32 Y = 0; Y < OwnershipBitmapSize; ++Y)
			{
				for (int32 X = 0; X < OwnershipBitmapSize; ++X)
				{
					const int32 Index = Y * OwnershipBitmapSize + X;
					int32 BestDistSq = MAX_int32;
					FJFASeed BestSeed = TempSeeds[Index];

					if (BestSeed.X >= 0)
					{
						const int32 DX = X - BestSeed.X;
						const int32 DY = Y - BestSeed.Y;
						BestDistSq = DX * DX + DY * DY;
					}

					// Check 8 neighbors at step distance
					for (int32 DY = -StepSize; DY <= StepSize; DY += StepSize)
					{
						for (int32 DX = -StepSize; DX <= StepSize; DX += StepSize)
						{
							if (DX == 0 && DY == 0)
							{
								continue;
							}

							const int32 NX = X + DX;
							const int32 NY = Y + DY;

							if (NX < 0 || NX >= OwnershipBitmapSize || NY < 0 || NY >= OwnershipBitmapSize)
							{
								continue;
							}

							const int32 NeighborIndex = NY * OwnershipBitmapSize + NX;
							const FJFASeed& NeighborSeed = TempSeeds[NeighborIndex];

							if (NeighborSeed.X < 0)
							{
								continue;
							}

							const int32 SeedDX = X - NeighborSeed.X;
							const int32 SeedDY = Y - NeighborSeed.Y;
							const int32 DistSq = SeedDX * SeedDX + SeedDY * SeedDY;

							if (DistSq < BestDistSq)
							{
								BestDistSq = DistSq;
								BestSeed = NeighborSeed;
							}
						}
					}

					Seeds[Index] = BestSeed;
				}
			}
		}

		// Write slot indices from seeds back into bitmap
		for (int32 Index = 0; Index < TotalPixels; ++Index)
		{
			if (Bitmap[Index] == UnclaimedSlotIndex && Seeds[Index].X >= 0)
			{
				const int32 SeedIndex = Seeds[Index].Y * OwnershipBitmapSize + Seeds[Index].X;
				Bitmap[Index] = Bitmap[SeedIndex];
			}
		}
	}

	// -------------------------------------------------------------------------
	// Phase 5: Copy body geometry onto outfit meshes
	// -------------------------------------------------------------------------

	/** Determines which slot owns a triangle based on majority vote of its 3 vertex UVs.
	 *  If all 3 are in different slots, uses the centroid UV as tiebreak. */
	static uint8 DetermineTriangleSlot(
		const TArray<uint8>& Bitmap,
		const FVector2f& UV0,
		const FVector2f& UV1,
		const FVector2f& UV2)
	{
		const uint8 Slot0 = LookupSlotForUV(Bitmap, UV0.X, UV0.Y);
		const uint8 Slot1 = LookupSlotForUV(Bitmap, UV1.X, UV1.Y);
		const uint8 Slot2 = LookupSlotForUV(Bitmap, UV2.X, UV2.Y);

		// If any two (or all three) agree, that's the majority
		if (Slot0 == Slot1 || Slot0 == Slot2)
		{
			return Slot0;
		}
		if (Slot1 == Slot2)
		{
			return Slot1;
		}

		// All three differ — use centroid as tiebreak
		const FVector2f Centroid = (UV0 + UV1 + UV2) / 3.0f;
		return LookupSlotForUV(Bitmap, Centroid.X, Centroid.Y);
	}


	/** Per-point tracking data for the geometry extraction, similar to MetaHumanGeometryRemoval.cpp */
	struct FPointData
	{
		FVector3f ShrinkDelta = FVector3f::ZeroVector;
		int32 ShrinkCount = 0;
		int32 StrippedVertexIndex_Shrunk = INDEX_NONE;
		int32 StrippedVertexIndex_Unshrunk = INDEX_NONE;
		bool bShouldBeCulled = false;
	};

	/**
	 * Rebakes the body geometry into the outfit's ref pose.
	 *
	 * For resizable outfits the two ref poses match, every delta is identity, and this is a cheap 
	 * no-op.
	 */
	static void RebakeBodyGeometryToOutfitRefPose(
		FMeshDescription& BodyGeometryMD,
		const FReferenceSkeleton& BodyRefSkel,
		const FReferenceSkeleton& OutfitRefSkel)
	{
		const int32 NumBodyBones = BodyRefSkel.GetNum();
		if (NumBodyBones == 0)
		{
			return;
		}

		// Build component-space ref-pose transforms for both skeletons. Ref skeletons
		// are topologically sorted (parent index < child index), so a single forward
		// pass composes each bone with its parent.
		auto BuildComponentSpacePose = [](const FReferenceSkeleton& RefSkel) -> TArray<FTransform>
		{
			const TArray<FTransform>& LocalPose = RefSkel.GetRawRefBonePose();
			const int32 NumBones = LocalPose.Num();
			TArray<FTransform> CSPose;
			CSPose.SetNum(NumBones);
			for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
			{
				const int32 ParentIdx = RefSkel.GetParentIndex(BoneIdx);
				if (ParentIdx == INDEX_NONE)
				{
					CSPose[BoneIdx] = LocalPose[BoneIdx];
				}
				else
				{
					// Row-vector convention: child_CS = child_local * parent_CS.
					CSPose[BoneIdx] = LocalPose[BoneIdx] * CSPose[ParentIdx];
				}
			}
			return CSPose;
		};

		const TArray<FTransform> BodyCS = BuildComponentSpacePose(BodyRefSkel);
		const TArray<FTransform> OutfitCS = BuildComponentSpacePose(OutfitRefSkel);

		// Per body-bone delta matrix in CS: V_outfit_cs = V_body_cs * (BodyCS^-1 * OutfitCS_match).
		TArray<FMatrix44f> BoneDeltaMatrices;
		BoneDeltaMatrices.SetNum(NumBodyBones);
		bool bAnyNonIdentity = false;
		constexpr float TranslationEpsilonSq = 1e-10f; // ~1e-5 units
		constexpr float RotationCosEpsilon = 1.0f - 1e-6f; // very tight

		for (int32 BodyBoneIdx = 0; BodyBoneIdx < NumBodyBones; ++BodyBoneIdx)
		{
			const FName BoneName = BodyRefSkel.GetBoneName(BodyBoneIdx);
			const int32 OutfitBoneIdx = OutfitRefSkel.FindBoneIndex(BoneName);

			FTransform Delta;
			if (OutfitBoneIdx != INDEX_NONE)
			{
				Delta = BodyCS[BodyBoneIdx].Inverse() * OutfitCS[OutfitBoneIdx];
			}
			// else: identity (default-constructed FTransform).

			BoneDeltaMatrices[BodyBoneIdx] = FMatrix44f(Delta.ToMatrixWithScale());

			if (!bAnyNonIdentity)
			{
				const FVector Translation = Delta.GetTranslation();
				const FQuat Rotation = Delta.GetRotation();
				const FVector Scale = Delta.GetScale3D();
				if (Translation.SizeSquared() > TranslationEpsilonSq ||
					FMath::Abs(Rotation.W) < RotationCosEpsilon ||
					!Scale.Equals(FVector::OneVector, 1e-5))
				{
					bAnyNonIdentity = true;
				}
			}
		}

		if (!bAnyNonIdentity)
		{
			return;
		}

		FSkeletalMeshConstAttributes BodyAttrs(BodyGeometryMD);
		FSkinWeightsVertexAttributesConstRef SkinWeights = BodyAttrs.GetVertexSkinWeights();
		if (!SkinWeights.IsValid())
		{
			return;
		}

		// Body MeshDescription's skin weights index into the bundle's RefSkeleton
		// (matches the convention used by the bone-remap and missing-bones blocks
		// surrounding this call).
		TVertexAttributesRef<FVector3f> VertexPositions = BodyGeometryMD.GetVertexPositions();
		FStaticMeshAttributes StaticAttrs(BodyGeometryMD);
		TVertexInstanceAttributesRef<FVector3f> VINormals = StaticAttrs.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> VITangents = StaticAttrs.GetVertexInstanceTangents();

		// Compute one blended LBS matrix per vertex and cache it for the per-vertex-instance
		// tangent pass
		TArray<FMatrix44f> PerVertexBlendMatrices;
		PerVertexBlendMatrices.SetNum(BodyGeometryMD.Vertices().GetArraySize());

		const FMatrix44f IdentityMatrix = FMatrix44f::Identity;

		for (const FVertexID VertexID : BodyGeometryMD.Vertices().GetElementIDs())
		{
			FVertexBoneWeightsConst Weights = SkinWeights.Get(VertexID);

			// Standard LBS: M(v) = sum_i W_i * Delta_i. Sum can drift slightly from 1.0
			// due to quantization in the import pipeline, so we accumulate a weight total
			// and renormalize the matrix at the end (cheap, makes the rebake invariant
			// under tiny weight drift and avoids visible scale artefacts).
			FMatrix44f Blended(ForceInitToZero);
			float TotalWeight = 0.0f;

			for (int32 Idx = 0; Idx < Weights.Num(); ++Idx)
			{
				const float Weight = Weights[Idx].GetWeight();
				if (Weight <= 0.0f)
				{
					continue;
				}
				const FBoneIndexType BoneIdx = Weights[Idx].GetBoneIndex();
				if (BoneIdx >= static_cast<FBoneIndexType>(NumBodyBones))
				{
					continue;
				}
				Blended += BoneDeltaMatrices[BoneIdx] * Weight;
				TotalWeight += Weight;
			}

			FMatrix44f VertexMatrix;
			if (TotalWeight > KINDA_SMALL_NUMBER)
			{
				if (FMath::Abs(TotalWeight - 1.0f) > 1e-4f)
				{
					Blended *= (1.0f / TotalWeight);
				}
				VertexMatrix = Blended;
			}
			else
			{
				VertexMatrix = IdentityMatrix;
			}

			PerVertexBlendMatrices[VertexID.GetValue()] = VertexMatrix;

			const FVector3f& OldPos = VertexPositions[VertexID];
			VertexPositions[VertexID] = VertexMatrix.TransformPosition(OldPos);
		}

		// Transform per-vertex-instance normals and tangents by the 3x3 part of the
		// blended matrix. Renormalize the new normal, then Gram-Schmidt the tangent
		// against it so the basis stays orthonormal under non-uniform delta scale.
		// Binormal sign is preserved -- the engine reconstructs the binormal from
		// (normal x tangent) * binormal_sign at runtime, and the delta's
		// orientation-preserving rotation keeps the handedness intact.
		for (const FVertexInstanceID VIID : BodyGeometryMD.VertexInstances().GetElementIDs())
		{
			const FVertexID VertexID = BodyGeometryMD.GetVertexInstanceVertex(VIID);
			const FMatrix44f& VertexMatrix = PerVertexBlendMatrices[VertexID.GetValue()];

			FVector3f Normal = VertexMatrix.TransformVector(VINormals[VIID]);
			Normal.Normalize();
			VINormals[VIID] = Normal;

			FVector3f Tangent = VertexMatrix.TransformVector(VITangents[VIID]);
			// Gram-Schmidt: remove the component of Tangent along Normal.
			Tangent -= Normal * FVector3f::DotProduct(Normal, Tangent);
			Tangent.Normalize();
			VITangents[VIID] = Tangent;
		}
	}

	/**
	 * Extracts body geometry for a specific slot from a body mesh LOD, filtered by
	 * the slot's ownership bitmap and a specific item's face map, then appends it
	 * to the outfit mesh.
	 */
	static void ExtractAndAppendBodyGeometry(
		const FMetaHumanCrowdMeshGeometryBundle& BodyBundle,
		int32 LODIndex,
		FMetaHumanCrowdMeshGeometryBundle& OutfitBundle,
		const TArray<uint8>& OwnershipBitmap,
		uint8 TargetSlotIndex,
		const FImage& FaceMapImage,
		const GeometryRemoval::FHiddenFaceMapSettings& FaceMapSettings,
		const USkeleton* TargetSkeleton)
	{
		// Get body mesh description from the bundle
		if (!BodyBundle.MeshDescriptions.IsValidIndex(LODIndex) || BodyBundle.MeshDescriptions[LODIndex].Vertices().Num() == 0)
		{
			return;
		}
		const FMeshDescription* BodyMeshDesc = &BodyBundle.MeshDescriptions[LODIndex];

		// Convert body mesh to import data for easier manipulation
		FSkeletalMeshImportData ImportedData = FSkeletalMeshImportData::CreateFromMeshDescription(*BodyMeshDesc);

		if (ImportedData.Faces.Num() == 0)
		{
			return;
		}

		// Determine which faces belong to this slot and should be kept
		TBitArray<> FaceToKeep;
		FaceToKeep.Init(false, ImportedData.Faces.Num());
		int32 KeptFaceCount = 0;

		for (int32 FaceIndex = 0; FaceIndex < ImportedData.Faces.Num(); ++FaceIndex)
		{
			const SkeletalMeshImportData::FTriangle& Face = ImportedData.Faces[FaceIndex];

			// Get UVs for the 3 wedges of this face
			const FVector2f UV0 = ImportedData.Wedges[Face.WedgeIndex[0]].UVs[0];
			const FVector2f UV1 = ImportedData.Wedges[Face.WedgeIndex[1]].UVs[0];
			const FVector2f UV2 = ImportedData.Wedges[Face.WedgeIndex[2]].UVs[0];

			// Determine which slot owns this triangle
			const uint8 TriangleSlot = DetermineTriangleSlot(OwnershipBitmap, UV0, UV1, UV2);

			if (TriangleSlot != TargetSlotIndex)
			{
				continue;
			}

			// Check if the triangle should be stripped by the face map
			if (FaceMapImage.SizeX > 0 && FaceMapImage.SizeY > 0)
			{
				if (GeometryRemoval::ShouldHideTriangle(UV0, UV1, UV2, FaceMapImage, FaceMapSettings.MaxCullValue))
				{
					continue;
				}
			}

			FaceToKeep[FaceIndex] = true;
			++KeptFaceCount;
		}

		if (KeptFaceCount == 0)
		{
			return;
		}

		// Build point data for shrink calculations
		TArray<FPointData> PointData;
		PointData.SetNum(ImportedData.Points.Num());

		const bool bHasValidFaceMap = (FaceMapImage.SizeX > 0 && FaceMapImage.SizeY > 0);
		const float MaxShrinkDistance = FaceMapSettings.MaxShrinkDistance;

		// Compute shrink data for all points used by kept faces
		if (bHasValidFaceMap && MaxShrinkDistance > 0.0f)
		{
			// We need vertex normals for shrink. Use the import data wedge tangents.
			// First pass: accumulate shrink deltas from all wedges referencing each point
			for (int32 FaceIndex = 0; FaceIndex < ImportedData.Faces.Num(); ++FaceIndex)
			{
				if (!FaceToKeep[FaceIndex])
				{
					continue;
				}

				const SkeletalMeshImportData::FTriangle& Face = ImportedData.Faces[FaceIndex];

				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					const int32 WedgeIndex = Face.WedgeIndex[Corner];
					const SkeletalMeshImportData::FVertex& Wedge = ImportedData.Wedges[WedgeIndex];
					const int32 PointIndex = Wedge.VertexIndex;
					const FVector2f& UV = Wedge.UVs[0];

					const FIntVector2 PixelCoord = GeometryRemoval::GetHiddenFaceMapPixelForUV(UV, static_cast<float>(FaceMapImage.SizeX), static_cast<float>(FaceMapImage.SizeY));
					const float PixVal = GeometryRemoval::SampleHiddenFaceMap(FaceMapImage, PixelCoord.X, PixelCoord.Y);
					const float ShrinkWeight = GeometryRemoval::ComputeShrinkWeight(PixVal, FaceMapSettings.MaxCullValue, FaceMapSettings.MinKeepValue);

					FPointData& Point = PointData[PointIndex];
					Point.bShouldBeCulled = GeometryRemoval::IsPixelCulled(PixVal, FaceMapSettings.MaxCullValue);

					if (ShrinkWeight > 0.0f)
					{
						// Use the wedge tangent Z (normal) for shrink direction
						Point.ShrinkDelta -= Face.TangentZ[Corner] * (MaxShrinkDistance * ShrinkWeight);
						Point.ShrinkCount++;
					}
				}
			}
		}

		// Build the filtered import data with only the kept faces
		FSkeletalMeshImportData FilteredData = ImportedData;
		FilteredData.Faces.Reset();
		FilteredData.Wedges.Reset();
		FilteredData.Points.Reset();
		FilteredData.PointToRawMap.Reset();
		FilteredData.Influences.Reset();

		FilteredData.Faces.Reserve(KeptFaceCount);
		FilteredData.Wedges.Reserve(KeptFaceCount * 3);

		int32 NewWedgeIndex = 0;

		for (int32 FaceIndex = 0; FaceIndex < ImportedData.Faces.Num(); ++FaceIndex)
		{
			if (!FaceToKeep[FaceIndex])
			{
				continue;
			}

			const SkeletalMeshImportData::FTriangle& OrigFace = ImportedData.Faces[FaceIndex];

			// Check if any non-culled point on this triangle has shrink applied
			bool bHasAnyUnculledShrunkPoints = false;
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const int32 PointIndex = ImportedData.Wedges[OrigFace.WedgeIndex[Corner]].VertexIndex;
				const FPointData& Point = PointData[PointIndex];
				if (!Point.bShouldBeCulled && Point.ShrinkCount > 0)
				{
					bHasAnyUnculledShrunkPoints = true;
					break;
				}
			}

			SkeletalMeshImportData::FTriangle& NewFace = FilteredData.Faces.AddDefaulted_GetRef();
			NewFace = OrigFace;

			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const int32 OrigWedgeIndex = OrigFace.WedgeIndex[Corner];
				const SkeletalMeshImportData::FVertex& OrigWedge = ImportedData.Wedges[OrigWedgeIndex];
				const int32 OrigPointIndex = OrigWedge.VertexIndex;
				const FPointData& Point = PointData[OrigPointIndex];

				const bool bShouldApplyShrink = (Point.ShrinkCount > 0 && bHasAnyUnculledShrunkPoints);

				// Determine which stripped vertex index to use (shrunk or unshrunk)
				int32& TargetStrippedIndex = bShouldApplyShrink
					? PointData[OrigPointIndex].StrippedVertexIndex_Shrunk
					: PointData[OrigPointIndex].StrippedVertexIndex_Unshrunk;

				if (TargetStrippedIndex == INDEX_NONE)
				{
					// First time encountering this point in this mode — create a new point
					TargetStrippedIndex = FilteredData.Points.Num();

					FVector3f NewPosition = ImportedData.Points[OrigPointIndex];
					if (bShouldApplyShrink)
					{
						NewPosition += Point.ShrinkDelta / static_cast<float>(Point.ShrinkCount);
					}

					FilteredData.Points.Add(NewPosition);

					if (OrigPointIndex < ImportedData.PointToRawMap.Num())
					{
						FilteredData.PointToRawMap.Add(ImportedData.PointToRawMap[OrigPointIndex]);
					}
					else
					{
						FilteredData.PointToRawMap.Add(OrigPointIndex);
					}
				}

				// Create the new wedge
				SkeletalMeshImportData::FVertex& NewWedge = FilteredData.Wedges.AddDefaulted_GetRef();
				NewWedge = OrigWedge;
				NewWedge.VertexIndex = TargetStrippedIndex;

				NewFace.WedgeIndex[Corner] = NewWedgeIndex;
				++NewWedgeIndex;
			}
		}

		// Remap bone influences
		for (int32 InfluenceIndex = 0; InfluenceIndex < ImportedData.Influences.Num(); ++InfluenceIndex)
		{
			const SkeletalMeshImportData::FRawBoneInfluence& OrigInfluence = ImportedData.Influences[InfluenceIndex];
			const FPointData& Point = PointData[OrigInfluence.VertexIndex];

			if (Point.StrippedVertexIndex_Shrunk != INDEX_NONE)
			{
				SkeletalMeshImportData::FRawBoneInfluence& NewInfluence = FilteredData.Influences.AddDefaulted_GetRef();
				NewInfluence = OrigInfluence;
				NewInfluence.VertexIndex = Point.StrippedVertexIndex_Shrunk;
			}

			if (Point.StrippedVertexIndex_Unshrunk != INDEX_NONE)
			{
				SkeletalMeshImportData::FRawBoneInfluence& NewInfluence = FilteredData.Influences.AddDefaulted_GetRef();
				NewInfluence = OrigInfluence;
				NewInfluence.VertexIndex = Point.StrippedVertexIndex_Unshrunk;
			}
		}

		if (FilteredData.Points.Num() == 0 || FilteredData.Faces.Num() == 0)
		{
			return;
		}

		// Convert filtered import data to mesh description
		FMeshDescription BodyGeometryMD;
		FSkeletalMeshAttributes BodyMeshAttributes(BodyGeometryMD);
		BodyMeshAttributes.Register();

		FSkeletalMeshBuildSettings DefaultBuildSettings;
		if (!FilteredData.GetMeshDescription(nullptr, &DefaultBuildSettings, BodyGeometryMD))
		{
			return;
		}

		// Get the outfit mesh description for this LOD (mutable pointer)
		FMeshDescription* OutfitMD = OutfitBundle.MeshDescriptions.IsValidIndex(LODIndex) ? &OutfitBundle.MeshDescriptions[LODIndex] : nullptr;
		if (!OutfitMD)
		{
			return;
		}

		// Record pre-append vertex count for skin weight offset
		const int32 OutfitVertexCountBeforeAppend = OutfitMD->Vertices().Num();

		// In a geometry bundle, the polygon group slot names stored on the
		// MeshDescription may be raw import names that do not match the canonical
		// MaterialSlotName on Bundle.Materials. Per-section material identity comes
		// from the LODMaterialMap, not the polygon group slot names.
		//
		// The rest of this function identifies materials by slot name when merging
		// the body geometry into the outfit, so rewrite each polygon group's slot
		// name to the canonical MaterialSlotName of the material it resolves to.
		// This runs on a local copy of the body MeshDescription and in-place on the
		// outfit MeshDescription (using its pre-merge bundle state) before anything
		// else is mutated.
		auto CanonicalizeMeshDescriptionPolygonGroupSlotNames = [](
			FMeshDescription& MD,
			const FMetaHumanCrowdMeshGeometryBundle& SourceBundle,
			int32 SourceLODIndex)
		{
			if (MD.PolygonGroups().Num() == 0)
			{
				return;
			}
			FStaticMeshAttributes Attrs(MD);
			TPolygonGroupAttributesRef<FName> SlotNames = Attrs.GetPolygonGroupMaterialSlotNames();
			for (const FPolygonGroupID PGID : MD.PolygonGroups().GetElementIDs())
			{
				const int32 BundleMatIdx = UE::MetaHuman::CrowdEditorUtilities::ResolveBundleMaterialIndex(
					SourceBundle, SourceLODIndex, PGID.GetValue());
				if (SourceBundle.Materials.IsValidIndex(BundleMatIdx))
				{
					SlotNames.Set(PGID, SourceBundle.Materials[BundleMatIdx].MaterialSlotName);
				}
			}
		};

		CanonicalizeMeshDescriptionPolygonGroupSlotNames(BodyGeometryMD, BodyBundle, LODIndex);
		CanonicalizeMeshDescriptionPolygonGroupSlotNames(*OutfitMD, OutfitBundle, LODIndex);

		// Merge body material slots into the outfit bundle, deduplicating by
		// MaterialSlotName. The LODMaterialMap for this LOD is rebuilt below once
		// the final polygon group order is known.
		{
			TArray<FSkeletalMaterial>& OutfitMaterials = OutfitBundle.Materials;
			const TArray<FSkeletalMaterial>& BodyMaterials = BodyBundle.Materials;

			for (const FSkeletalMaterial& BodyMaterial : BodyMaterials)
			{
				bool bAlreadyExists = false;
				for (const FSkeletalMaterial& OutfitMaterial : OutfitMaterials)
				{
					if (OutfitMaterial.MaterialSlotName == BodyMaterial.MaterialSlotName)
					{
						bAlreadyExists = true;
						break;
					}
				}

				if (!bAlreadyExists)
				{
					OutfitMaterials.Add(BodyMaterial);
				}
			}
		}

		// The outfit skeleton may be missing bones that the appended body geometry
		// needs -- for example, an upper-garment outfit won't reference leg or foot
		// bones, but the body geometry being merged in may. Add any bones
		// referenced by that body geometry (and their ancestors, so the hierarchy
		// stays valid) to the outfit's ref skeleton and MeshDescription before the
		// append, so the skin weight bone remap can resolve every body bone.
		{
			const FReferenceSkeleton& BodyRefSkel = BodyBundle.RefSkeleton;
			FReferenceSkeleton& OutfitRefSkel = OutfitBundle.RefSkeleton;

			FSkeletalMeshAttributes OutfitMDAttrs(*OutfitMD);
			FSkeletalMeshAttributesShared::FBoneNameAttributesRef OutfitBoneNames = OutfitMDAttrs.GetBoneNames();
			FSkeletalMeshAttributes::FBoneParentIndexAttributesRef OutfitBoneParents = OutfitMDAttrs.GetBoneParentIndices();
			FSkeletalMeshAttributes::FBonePoseAttributesRef OutfitBonePoses = OutfitMDAttrs.GetBonePoses();

			// Collect body bones that are missing from the outfit. We need each missing bone
			// plus its full ancestor chain so the skeleton hierarchy stays valid.
			TSet<int32> BonesNeeded; // Indices into BodyRefSkel

			// Gather all bones referenced by the filtered body geometry's skin weights.
			FSkeletalMeshConstAttributes BodyMDAttrs(BodyGeometryMD);
			FSkinWeightsVertexAttributesConstRef BodySkinWeights = BodyMDAttrs.GetVertexSkinWeights();
			for (const FVertexID VertexID : BodyGeometryMD.Vertices().GetElementIDs())
			{
				FVertexBoneWeightsConst Weights = BodySkinWeights.Get(VertexID);
				for (int32 Idx = 0; Idx < Weights.Num(); ++Idx)
				{
					const FBoneIndexType BoneIdx = Weights[Idx].GetBoneIndex();
					if (BoneIdx < static_cast<FBoneIndexType>(BodyRefSkel.GetNum()))
					{
						BonesNeeded.Add(BoneIdx);
					}
				}
			}

			// For each needed bone, also add its ancestors up to the root.
			TArray<int32> BonesToProcess = BonesNeeded.Array();
			for (int32 Idx = 0; Idx < BonesToProcess.Num(); ++Idx)
			{
				const int32 ParentIdx = BodyRefSkel.GetParentIndex(BonesToProcess[Idx]);
				if (ParentIdx != INDEX_NONE && !BonesNeeded.Contains(ParentIdx))
				{
					BonesNeeded.Add(ParentIdx);
					BonesToProcess.Add(ParentIdx);
				}
			}

			// Determine which needed bones are missing from the outfit.
			TArray<int32> MissingBodyBoneIndices;
			for (const int32 BodyBoneIdx : BonesNeeded)
			{
				const FName BoneName = BodyRefSkel.GetBoneName(BodyBoneIdx);
				if (OutfitRefSkel.FindBoneIndex(BoneName) == INDEX_NONE)
				{
					MissingBodyBoneIndices.Add(BodyBoneIdx);
				}
			}

			// Sort missing bones by body skeleton index so parents are added before children.
			MissingBodyBoneIndices.Sort();

			// Add missing bones to the outfit's ref skeleton and MeshDescription.
			if (MissingBodyBoneIndices.Num() > 0)
			{
				{
					FReferenceSkeletonModifier Modifier(OutfitRefSkel, TargetSkeleton);

					for (const int32 BodyBoneIdx : MissingBodyBoneIndices)
					{
						const FName BoneName = BodyRefSkel.GetBoneName(BodyBoneIdx);
						const FTransform& BonePose = BodyRefSkel.GetRawRefBonePose()[BodyBoneIdx];

						// Find the parent's index in the outfit skeleton.
						int32 OutfitParentIdx = INDEX_NONE;
						const int32 BodyParentIdx = BodyRefSkel.GetParentIndex(BodyBoneIdx);
						if (BodyParentIdx != INDEX_NONE)
						{
							const FName ParentName = BodyRefSkel.GetBoneName(BodyParentIdx);
							OutfitParentIdx = Modifier.FindBoneIndex(ParentName);
							// Parent must already exist (we sorted by index, and we added all ancestors).
							check(OutfitParentIdx != INDEX_NONE);
						}

						FMeshBoneInfo BoneInfo;
						BoneInfo.Name = BoneName;
						BoneInfo.ParentIndex = OutfitParentIdx;
						Modifier.Add(BoneInfo, BonePose);

						// Also add the bone to the outfit's MeshDescription so bone indices stay in sync.
						const FBoneID NewBoneID = OutfitMDAttrs.CreateBone();
						OutfitBoneNames.Set(NewBoneID, BoneName);
						OutfitBoneParents.Set(NewBoneID, OutfitParentIdx);
						OutfitBonePoses.Set(NewBoneID, BonePose);
					}

					UE_LOGFMT(LogCrowdBodyMerge, Log, "CrowdBodyMerge: Added {NumBones} missing body bones to outfit bundle for LOD {LOD}.",
						MissingBodyBoneIndices.Num(), LODIndex);
				}
			}
		}

		RebakeBodyGeometryToOutfitRefPose(BodyGeometryMD, BodyBundle.RefSkeleton, OutfitBundle.RefSkeleton);

		// Append body geometry into the outfit MeshDescription. Skin weights are
		// handled separately below. AppendMeshDescription matches polygon groups by
		// slot name; the canonicalization above ensures that match uses the same
		// names as OutfitBundle.Materials.
		FStaticMeshOperations::FAppendSettings AppendSettings;
		FStaticMeshOperations::AppendMeshDescription(BodyGeometryMD, *OutfitMD, AppendSettings);

		// Rebuild LODMaterialMaps for this LOD by name lookup: for each polygon
		// group in the merged MeshDescription, find the Materials entry whose
		// MaterialSlotName matches the polygon group's canonical slot name and
		// write its index. AppendMeshDescription does not guarantee that the
		// post-append polygon group order matches the Materials order, so a
		// name-keyed rebuild is the only way to keep each polygon group's material
		// identity correct.
		{
			if (!OutfitBundle.LODMaterialMaps.IsValidIndex(LODIndex))
			{
				OutfitBundle.LODMaterialMaps.SetNum(FMath::Max(OutfitBundle.LODMaterialMaps.Num(), LODIndex + 1));
			}
			TArray<int32>& RebuiltMap = OutfitBundle.LODMaterialMaps[LODIndex];
			const int32 NumPGs = OutfitMD->PolygonGroups().Num();
			RebuiltMap.SetNum(NumPGs);

			// Build a slot-name -> OutfitBundle.Materials index map for O(1) lookup.
			TMap<FName, int32> OutfitSlotNameToIndex;
			OutfitSlotNameToIndex.Reserve(OutfitBundle.Materials.Num());
			for (int32 MatIdx = 0; MatIdx < OutfitBundle.Materials.Num(); ++MatIdx)
			{
				OutfitSlotNameToIndex.FindOrAdd(OutfitBundle.Materials[MatIdx].MaterialSlotName, MatIdx);
			}

			FStaticMeshAttributes OutfitStaticAttrs(*OutfitMD);
			TPolygonGroupAttributesConstRef<FName> OutfitSlotNames = OutfitStaticAttrs.GetPolygonGroupMaterialSlotNames();

			for (const FPolygonGroupID PGID : OutfitMD->PolygonGroups().GetElementIDs())
			{
				const int32 PGIdx = PGID.GetValue();
				const FName SlotName = OutfitSlotNames.Get(PGID);
				if (const int32* FoundMatIdx = OutfitSlotNameToIndex.Find(SlotName))
				{
					RebuiltMap[PGIdx] = *FoundMatIdx;
				}
				else
				{
					// Canonicalization plus the body-material merge should make every PG's
					// slot name resolvable in OutfitBundle.Materials. Reaching this branch
					// means the inputs didn't meet that contract -- typically a malformed
					// source bundle that left a PG's slot name in its raw import form.
					// Synthesize a stub material entry keyed by the canonical slot name so
					// the rebuilt map points at a valid index rather than an arbitrary one.
					UE_LOGFMT(LogCrowdBodyMerge, Warning,
						"CrowdBodyMerge: No OutfitBundle.Materials entry found for PG slot '{SlotName}' at LOD {LOD} after append. "
						"Synthesizing stub material entry at index {NewMatIdx}.",
						SlotName, LODIndex, OutfitBundle.Materials.Num());
					FSkeletalMaterial Stub;
					Stub.MaterialSlotName = SlotName;
					const int32 NewMatIdx = OutfitBundle.Materials.Add(Stub);
					OutfitSlotNameToIndex.Add(SlotName, NewMatIdx);
					RebuiltMap[PGIdx] = NewMatIdx;
				}
			}
		}

		// Build bone index remap: body mesh bones → outfit mesh bones.
		// Now that missing bones have been added, every body bone should be found.
		const FReferenceSkeleton& BodyRefSkel = BodyBundle.RefSkeleton;
		const FReferenceSkeleton& OutfitRefSkel = OutfitBundle.RefSkeleton;

		TArray<FBoneIndexType> BoneRemap;
		BoneRemap.SetNum(BodyRefSkel.GetNum());

		for (int32 BodyBoneIndex = 0; BodyBoneIndex < BodyRefSkel.GetNum(); ++BodyBoneIndex)
		{
			const FName BoneName = BodyRefSkel.GetBoneName(BodyBoneIndex);
			const int32 OutfitBoneIndex = OutfitRefSkel.FindBoneIndex(BoneName);

			if (OutfitBoneIndex != INDEX_NONE)
			{
				BoneRemap[BodyBoneIndex] = static_cast<FBoneIndexType>(OutfitBoneIndex);
			}
			else
			{
				UE_LOGFMT(LogCrowdBodyMerge, Warning, "CrowdBodyMerge: Bone '{Bone}' from body mesh not found in outfit mesh skeleton after adding missing bones. Mapping to root bone.",
					BoneName);
				BoneRemap[BodyBoneIndex] = 0;
			}
		}

		// Append skin weights
		FSkeletalMeshOperations::FSkeletalMeshAppendSettings SkelAppendSettings;
		SkelAppendSettings.SourceVertexIDOffset = OutfitVertexCountBeforeAppend;
		SkelAppendSettings.SourceRemapBoneIndex = BoneRemap;

		FSkeletalMeshOperations::AppendSkinWeight(BodyGeometryMD, *OutfitMD, SkelAppendSettings);
	}

	// -------------------------------------------------------------------------
	// Debug: write ownership bitmap as a colored PNG
	// -------------------------------------------------------------------------

	static void WriteDebugOwnershipImage(
		const TArray<uint8>& Bitmap,
		const TArray<FSlotData>& Slots,
		const FString& Path)
	{
		FImage DebugImage(OwnershipBitmapSize, OwnershipBitmapSize, 1, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		FColor* Pixels = reinterpret_cast<FColor*>(DebugImage.RawData.GetData());

		for (int32 Index = 0; Index < OwnershipBitmapSize * OwnershipBitmapSize; ++Index)
		{
			const uint8 SlotIdx = Bitmap[Index];
			if (SlotIdx == UnclaimedSlotIndex)
			{
				Pixels[Index] = FColor::Black;
			}
			else
			{
				Pixels[Index] = (SlotIdx < static_cast<uint8>(Slots.Num())) ? Slots[SlotIdx].SlotColor.ToFColor(true) : FColor::White;
			}
		}

		FImageUtils::SaveImageByExtension(*Path, DebugImage);
		UE_LOGFMT(LogCrowdBodyMerge, Log, "CrowdBodyMerge: Wrote debug image to: {Path}", Path);
	}

	// -------------------------------------------------------------------------
	// Main entry point
	// -------------------------------------------------------------------------

	void MergeBodyOntoOutfits(
		const TMap<FMetaHumanPaletteItemKey, FMetaHumanCrowdMeshGeometryBundle*>& BodyGeometryBundles,
		TArray<FSlotData>& Slots,
		TConstArrayView<int32> LODIndicesToProcess,
		const USkeleton* TargetSkeleton,
		const FString& DebugImagePath)
	{
		if (Slots.Num() == 0 || BodyGeometryBundles.Num() == 0 || LODIndicesToProcess.Num() == 0)
		{
			return;
		}

		// Phase 1: Build slot ownership bitmap
		TArray<uint8> OwnershipBitmap;
		BuildSlotOwnershipBitmap(OwnershipBitmap, Slots);

		// Pick a body bundle + LOD to seed Phases 2-3 (all bodies share UV layout, so any
		// bundle/LOD with vertices produces a valid UV-space ownership bitmap that applies
		// to every body in Phase 5). Iterate bundles until we find one with a usable LOD
		// from LODIndicesToProcess -- don't commit to the first non-empty bundle if none
		// of its populated LODs are in range, or we'd silently skip the merge.
		const FMetaHumanCrowdMeshGeometryBundle* FirstBodyBundle = nullptr;
		int32 FirstBodyLODIndex = INDEX_NONE;
		for (const TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdMeshGeometryBundle*>& Pair : BodyGeometryBundles)
		{
			if (!Pair.Value || Pair.Value->MeshDescriptions.Num() == 0)
			{
				continue;
			}

			for (int32 LODIndex : LODIndicesToProcess)
			{
				if (Pair.Value->MeshDescriptions.IsValidIndex(LODIndex) &&
					Pair.Value->MeshDescriptions[LODIndex].Vertices().Num() > 0)
				{
					FirstBodyBundle = Pair.Value;
					FirstBodyLODIndex = LODIndex;
					break;
				}
			}

			if (FirstBodyLODIndex != INDEX_NONE)
			{
				break;
			}
		}

		if (!FirstBodyBundle || FirstBodyLODIndex == INDEX_NONE)
		{
			return;
		}

		const FMeshDescription& FirstBodyMeshDesc = FirstBodyBundle->MeshDescriptions[FirstBodyLODIndex];

		const bool bWriteDebugImages = CVarDebugBodyMergeSlotOwnership.GetValueOnAnyThread() && !DebugImagePath.IsEmpty();

		// Phase 2: Assign vertices to slots and build per-slot max bone weights
		TArray<int32> UnclaimedVerts;
		AssignVerticesToSlots(OwnershipBitmap, Slots, FirstBodyMeshDesc, FirstBodyBundle->RefSkeleton, UnclaimedVerts);

		if (bWriteDebugImages)
		{
			const FString BasePath = FPaths::GetBaseFilename(DebugImagePath, false);
			WriteDebugOwnershipImage(OwnershipBitmap, Slots, BasePath + TEXT("_1_AfterFaceMaps.png"));
		}

		// Phase 3: Resolve unclaimed vertices via bone hierarchy
		if (UnclaimedVerts.Num() > 0)
		{
			ResolveUnclaimedVerts(OwnershipBitmap, UnclaimedVerts, Slots, FirstBodyMeshDesc, FirstBodyBundle->RefSkeleton);
		}

		if (bWriteDebugImages)
		{
			const FString BasePath = FPaths::GetBaseFilename(DebugImagePath, false);
			WriteDebugOwnershipImage(OwnershipBitmap, Slots, BasePath + TEXT("_2_AfterBoneResolve.png"));
		}

		// Phase 4: Flood fill remaining unclaimed pixels
		FloodFillBitmap(OwnershipBitmap);

		if (bWriteDebugImages)
		{
			WriteDebugOwnershipImage(OwnershipBitmap, Slots, DebugImagePath);
		}

		// Phase 5: For each body, for each LOD, for each slot's items, extract and append body geometry
		for (const TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdMeshGeometryBundle*>& BodyPair : BodyGeometryBundles)
		{
			const FMetaHumanCrowdMeshGeometryBundle* BodyBundle = BodyPair.Value;
			if (!BodyBundle)
			{
				continue;
			}

			for (int32 SlotIdx = 0; SlotIdx < Slots.Num(); ++SlotIdx)
			{
				FSlotData& Slot = Slots[SlotIdx];
				for (FSlotItemData& Item : Slot.Items)
				{
					FMetaHumanCrowdMeshGeometryBundle** OutfitBundlePtr = Item.BodyToOutfitGeometryMap.Find(BodyPair.Key);
					if (!OutfitBundlePtr || !*OutfitBundlePtr)
					{
						continue;
					}

					FMetaHumanCrowdMeshGeometryBundle& OutfitBundle = **OutfitBundlePtr;

					for (int32 LODIndex : LODIndicesToProcess)
					{
						if (!BodyBundle->MeshDescriptions.IsValidIndex(LODIndex) ||
							BodyBundle->MeshDescriptions[LODIndex].Vertices().Num() == 0)
						{
							continue;
						}

						// Use this item's face map if available, otherwise use an empty image
						// (which means no geometry will be stripped — all owned geometry is added)
						const FImage EmptyImage;
						const GeometryRemoval::FHiddenFaceMapSettings DefaultSettings;

						const FImage& FaceMapImage = Item.bHasValidFaceMap
							? Item.BodyHiddenFaceMapImage.Image
							: EmptyImage;
						const GeometryRemoval::FHiddenFaceMapSettings& FaceMapSettings = Item.bHasValidFaceMap
							? Item.BodyHiddenFaceMapImage.Settings
							: DefaultSettings;

						ExtractAndAppendBodyGeometry(
							*BodyBundle,
							LODIndex,
							OutfitBundle,
							OwnershipBitmap,
							static_cast<uint8>(SlotIdx),
							FaceMapImage,
							FaceMapSettings,
							TargetSkeleton);
					}
				}
			}
		}
	}

} // namespace UE::MetaHuman::CrowdBodyMerge

