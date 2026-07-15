// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "UObject/NameTypes.h"
#include "UObject/ObjectPtr.h"

struct FMetaHumanMaterialParameter;
struct FMetaHumanCrowdOutfitCustomDataFormat;
struct FMetaHumanCrowdOutfitInstancedMaterial;
struct FInstancedPropertyBag;

namespace UE::MetaHuman::MaterialUtils
{
	/**
	 * Updates per-instance custom data floats for instanced mesh material slots.
	 *
	 * @param InMaterialParameters                   Parameter definitions from the source pipeline.
	 * @param InInstancedComponentOverrideMaterials  Per-slot instanced material configuration, keyed
	 *                                               by material slot name.
	 * @param InPropertyBag                          Current parameter values.
	 * @param OutCustomData                          Custom data float array to write into.
	 */
	void SetInstanceParametersOnCustomData(
		const TArray<FMetaHumanMaterialParameter>& InMaterialParameters,
		const TMap<FName, FMetaHumanCrowdOutfitInstancedMaterial>& InInstancedComponentOverrideMaterials,
		const FInstancedPropertyBag& InPropertyBag,
		TArrayView<float> OutCustomData);

	/**
	 * Seeds OutCustomData with default parameter values read from each slot's
	 * InstancedComponentMaterial.
	 *
	 * @param InMaterialParameters                   Parameter definitions from the source pipeline.
	 * @param InInstancedComponentOverrideMaterials  Per-slot instanced material configuration, keyed
	 *                                               by material slot name.
	 * @param OutCustomData                          Custom data float array to write into.
	 */
	void SetInstanceParameterDefaultsOnCustomData(
		const TArray<FMetaHumanMaterialParameter>& InMaterialParameters,
		const TMap<FName, FMetaHumanCrowdOutfitInstancedMaterial>& InInstancedComponentOverrideMaterials,
		TArrayView<float> OutCustomData);

	/**
	 * Returns a copy of InParameters containing only entries whose SlotTarget is supported by the
	 * crowd pipelines.
	 * 
	 * If bLogWarningOnFilter is true, logs a warning for each parameter that is filtered out.
	 */
	TArray<FMetaHumanMaterialParameter> FilterToCrowdSupportedParameters(
		TConstArrayView<FMetaHumanMaterialParameter> InParameters,
		bool bLogWarningOnFilter = false,
		const FString& InContextForLogging = TEXT(""));

	/**
	 * Computes the number of custom data floats required to hold every parameter described
	 * by InFormatMap.
	 */
	int32 ComputeCustomDataSize(
		const TMap<FName, FMetaHumanCrowdOutfitCustomDataFormat>& InFormatMap);

	/**
	 * Computes the total number of custom data floats needed to fit the authored custom data 
	 * offsets for every material slot on a mesh.
	 *
	 * The returned size is large enough that an offset+channel write from any slot is
	 * in-range. Callers should also run ValidateNoOverlappingCustomDataOffsets to ensure two
	 * slots haven't authored offsets that step on each other.
	 */
	int32 ComputeISKMCustomDataSize(
		const TMap<FName, FMetaHumanCrowdOutfitInstancedMaterial>& InInstancedComponentOverrideMaterials);

	/**
	 * Walks every slot's InstanceParameterNameToCustomDataFormat and warns if two slots write
	 * into overlapping float ranges of the ISKM-wide custom-data buffer.
	 *
	 * Validation is scoped to the slot names actually present on the mesh being assembled
	 * (InMeshSlotNames).
	 *
	 * Returns true if the layout is valid (no overlaps), false if collisions were found.
	 */
	bool ValidateNoOverlappingCustomDataOffsets(
		const TMap<FName, FMetaHumanCrowdOutfitInstancedMaterial>& InInstancedComponentOverrideMaterials,
		TConstArrayView<FName> InMeshSlotNames,
		const FString& InContextForLogging = TEXT(""));
}
