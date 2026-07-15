// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/NotNull.h"

class UMaterialInterface;
class UObject;
class USkeletalMesh;
struct FMetaHumanCrowdFaceMaterialOverride;
struct FMetaHumanCrowdFaceSlotLODs;

namespace UE::MetaHuman::CrowdMaterialEditorUtils
{
	/**
	 * Walks InMesh's imported model and produces a per-slot list of source-LOD indices the slot
	 * covers. InVariantSourceLODs maps each output-LOD index in InMesh to the source-LOD index
	 * the build picked for it (e.g. for a list of {4, 6}, output LOD 0 -> source LOD 4 and
	 * output LOD 1 -> source LOD 6).
	 *
	 * The map is consumed at assembly time to decide whether a per-slot parameter binding
	 * applies to a given MID: a slot whose source-LOD coverage doesn't intersect the binding's
	 * applicable LODs is skipped to avoid writing overrides on unrelated LODs.
	 */
	void BuildSlotSourceLODsMap(
		TNotNull<USkeletalMesh*> InMesh,
		TConstArrayView<int32> InVariantSourceLODs,
		TMap<FName, FMetaHumanCrowdFaceSlotLODs>& OutSlotToSourceLODs);

	/**
	 * Which face-mesh variant a material-override pass is targeting. Selects which of the
	 * override's Actor/Instanced soft material pointers wins, and drives generated MIC names.
	 */
	enum class EFaceMeshVariant : uint8
	{
		Actor,
		Instanced,
	};

	/**
	 * Replaces material slots on InMesh whose name matches an entry in InOverrides with a fresh
	 * UMaterialInstanceConstant parented to the override material. Scalar, vector and texture
	 * parameters are copied from the existing slot MIC so any per-character data (e.g.
	 * synthesised textures, makeup tweaks) survives the swap.
	 *
	 * Slots without a matching override, or whose entry has a null Material, are left
	 * untouched. The new MIC is named "MI_{InMICNamePrefix}_{InAssetName}_{SlotName}_{VariantSuffix}",
	 * where VariantSuffix is "Actor" or "Inst" derived from InVariant.
	 */
	void ApplySlotMaterialOverrides(
		TNotNull<USkeletalMesh*> InMesh,
		const TArray<FMetaHumanCrowdFaceMaterialOverride>& InOverrides,
		const FString& InMICNamePrefix,
		const FString& InAssetName,
		EFaceMeshVariant InVariant,
		UObject* InOuter);

	/**
	 * Replaces material slots on InMesh with InMaskMaterial when the slot is only referenced by
	 * source LODs >= InMinLOD. Geometry still renders, but the mask material produces no visible
	 * output (transparent/masked), letting another renderer take over those LODs without
	 * competing geometry.
	 *
	 * InVariantSourceLODs maps output-LOD index -> source LOD index, matching the order
	 * ConstructMeshFromBundle consumed when building InMesh.
	 */
	void ApplyMaskAtHighLODs(
		TNotNull<USkeletalMesh*> InMesh,
		TConstArrayView<int32> InVariantSourceLODs,
		int32 InMinLOD,
		UMaterialInterface* InMaskMaterial);
}
