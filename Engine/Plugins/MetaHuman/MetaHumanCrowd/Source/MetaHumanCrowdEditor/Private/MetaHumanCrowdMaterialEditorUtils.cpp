// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCrowdMaterialEditorUtils.h"

#include "MetaHumanCrowdEditorLog.h"
#include "MetaHumanCrowdEditorPipeline.h"
#include "MetaHumanCrowdPipeline.h"

#include "Engine/SkeletalMesh.h"
#include "Logging/StructuredLog.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameters.h"
#include "MetaHumanCharacterPaletteUnpackHelpers.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"

namespace UE::MetaHuman::CrowdMaterialEditorUtils
{
	void BuildSlotSourceLODsMap(
		TNotNull<USkeletalMesh*> InMesh,
		TConstArrayView<int32> InVariantSourceLODs,
		TMap<FName, FMetaHumanCrowdFaceSlotLODs>& OutSlotToSourceLODs)
	{
		const FSkeletalMeshModel* ImportedModel = InMesh->GetImportedModel();

		if (!ImportedModel)
		{
			return;
		}

		const TArray<FSkeletalMaterial>& Materials = InMesh->GetMaterials();

		for (int32 OutputLODIndex = 0; OutputLODIndex < ImportedModel->LODModels.Num(); ++OutputLODIndex)
		{
			if (!InVariantSourceLODs.IsValidIndex(OutputLODIndex))
			{
				continue;
			}

			const int32 SourceLODIndex = InVariantSourceLODs[OutputLODIndex];
			const FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[OutputLODIndex];
			const FSkeletalMeshLODInfo* LODInfo = InMesh->GetLODInfo(OutputLODIndex);

			for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); ++SectionIndex)
			{
				const FSkelMeshSection& Section = LODModel.Sections[SectionIndex];
				int32 EffectiveMaterialIndex = Section.MaterialIndex;

				if (LODInfo && LODInfo->LODMaterialMap.IsValidIndex(SectionIndex)
					&& LODInfo->LODMaterialMap[SectionIndex] != INDEX_NONE)
				{
					EffectiveMaterialIndex = LODInfo->LODMaterialMap[SectionIndex];
				}

				if (Materials.IsValidIndex(EffectiveMaterialIndex))
				{
					const FName MaterialSlotName = Materials[EffectiveMaterialIndex].MaterialSlotName;
					OutSlotToSourceLODs.FindOrAdd(MaterialSlotName).SourceLODs.AddUnique(SourceLODIndex);
				}
			}
		}
	}

	void ApplySlotMaterialOverrides(
		TNotNull<USkeletalMesh*> InMesh,
		const TArray<FMetaHumanCrowdFaceMaterialOverride>& InOverrides,
		const FString& InMICNamePrefix,
		const FString& InAssetName,
		EFaceMeshVariant InVariant,
		UObject* InOuter)
	{
		if (InOverrides.IsEmpty())
		{
			return;
		}

		const bool bIsInstancedVariant = InVariant == EFaceMeshVariant::Instanced;
		const FString VariantSuffix = bIsInstancedVariant ? TEXT("Inst") : TEXT("Actor");

		auto SelectVariantSoftPtr = [bIsInstancedVariant](const FMetaHumanCrowdFaceMaterialOverride& Override) -> const TSoftObjectPtr<UMaterialInterface>&
		{
			return bIsInstancedVariant ? Override.InstancedMaterial : Override.ActorMaterial;
		};

		// Build a slot-name -> material lookup, warning on duplicates (first wins).
		TMap<FName, UMaterialInterface*> OverrideBySlot;
		OverrideBySlot.Reserve(InOverrides.Num());
		for (const FMetaHumanCrowdFaceMaterialOverride& Override : InOverrides)
		{
			const TSoftObjectPtr<UMaterialInterface>& VariantMaterial = SelectVariantSoftPtr(Override);
			if (Override.SlotName.IsNone() || VariantMaterial.IsNull())
			{
				continue;
			}

			UMaterialInterface* LoadedMaterial = VariantMaterial.LoadSynchronous();
			if (!LoadedMaterial)
			{
				UE_LOGFMT(LogMetaHumanCrowdEditor, Warning,
					"FaceMaterialOverrides: failed to load material for slot {Slot}; skipping override.",
					Override.SlotName);
				continue;
			}

			if (OverrideBySlot.Contains(Override.SlotName))
			{
				UE_LOGFMT(LogMetaHumanCrowdEditor, Warning,
					"FaceMaterialOverrides: duplicate entry for slot {Slot}; keeping the first.",
					Override.SlotName);
				continue;
			}

			OverrideBySlot.Add(Override.SlotName, LoadedMaterial);
		}

		if (OverrideBySlot.IsEmpty())
		{
			return;
		}

		TArray<FSkeletalMaterial> Materials = InMesh->GetMaterials();
		bool bAnyChanged = false;

		for (FSkeletalMaterial& SlotEntry : Materials)
		{
			UMaterialInterface* const* FoundOverride = OverrideBySlot.Find(SlotEntry.MaterialSlotName);
			if (!FoundOverride || !*FoundOverride)
			{
				continue;
			}

			UMaterialInterface* SourceMaterial = SlotEntry.MaterialInterface;
			if (!SourceMaterial)
			{
				UE_LOGFMT(LogMetaHumanCrowdEditor, Warning,
					"FaceMaterialOverrides: face mesh slot {Slot} has no source material, skipping.",
					SlotEntry.MaterialSlotName);
				continue;
			}

			const FString DesiredName = FString::Format(TEXT("MI_{0}_{1}_{2}_{3}"),
				{ InMICNamePrefix, InAssetName, SlotEntry.MaterialSlotName.ToString(), VariantSuffix });
			UMaterialInstanceConstant* NewMIC = NewObject<UMaterialInstanceConstant>(InOuter, FName(*DesiredName), RF_Public);
			NewMIC->SetParentEditorOnly(*FoundOverride);

			using namespace UE::MetaHuman::PaletteUnpackHelpers;
			CopyMaterialParametersIfNeeded(EMaterialParameterType::Scalar, SourceMaterial, NewMIC);
			CopyMaterialParametersIfNeeded(EMaterialParameterType::Vector, SourceMaterial, NewMIC);
			CopyMaterialParametersIfNeeded(EMaterialParameterType::Texture, SourceMaterial, NewMIC);
			CopyMaterialParametersIfNeeded(EMaterialParameterType::StaticSwitch, SourceMaterial, NewMIC);

			// This is needed to ensure that static switches are applied
			NewMIC->PostEditChange();

			SlotEntry.MaterialInterface = NewMIC;
			bAnyChanged = true;
		}

		if (bAnyChanged)
		{
			InMesh->SetMaterials(Materials);
		}
	}

	void ApplyMaskAtHighLODs(
		TNotNull<USkeletalMesh*> InMesh,
		TConstArrayView<int32> InVariantSourceLODs,
		int32 InMinLOD,
		UMaterialInterface* InMaskMaterial)
	{
		if (!InMaskMaterial || InMinLOD == INT32_MAX)
		{
			return;
		}

		const TArray<FSkeletalMaterial>& Materials = InMesh->GetMaterials();

		// Collect, per material slot index, the set of source LODs that reference the slot. A slot
		// is "high-LOD only" when every referencing LOD is >= the threshold; only those slots get
		// masked. Slots referenced by both low and high LODs keep their original material so
		// low-LOD rendering stays correct.
		TArray<TArray<int32>> SlotIndexToSourceLODs;
		SlotIndexToSourceLODs.SetNum(Materials.Num());

		if (const FSkeletalMeshModel* ImportedModel = InMesh->GetImportedModel())
		{
			for (int32 OutputLODIndex = 0; OutputLODIndex < ImportedModel->LODModels.Num(); ++OutputLODIndex)
			{
				if (!InVariantSourceLODs.IsValidIndex(OutputLODIndex))
				{
					continue;
				}

				const int32 SourceLODIndex = InVariantSourceLODs[OutputLODIndex];
				const FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[OutputLODIndex];
				const FSkeletalMeshLODInfo* LODInfo = InMesh->GetLODInfo(OutputLODIndex);

				for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); ++SectionIndex)
				{
					const FSkelMeshSection& Section = LODModel.Sections[SectionIndex];
					int32 EffectiveMaterialIndex = Section.MaterialIndex;

					if (LODInfo && LODInfo->LODMaterialMap.IsValidIndex(SectionIndex)
						&& LODInfo->LODMaterialMap[SectionIndex] != INDEX_NONE)
					{
						EffectiveMaterialIndex = LODInfo->LODMaterialMap[SectionIndex];
					}

					if (SlotIndexToSourceLODs.IsValidIndex(EffectiveMaterialIndex))
					{
						SlotIndexToSourceLODs[EffectiveMaterialIndex].AddUnique(SourceLODIndex);
					}
				}
			}
		}

		TArray<FSkeletalMaterial> NewMaterials = Materials;
		bool bAnyChanged = false;

		for (int32 SlotIndex = 0; SlotIndex < NewMaterials.Num(); ++SlotIndex)
		{
			const TArray<int32>& SourceLODs = SlotIndexToSourceLODs[SlotIndex];
			if (SourceLODs.IsEmpty())
			{
				continue;
			}

			const bool bAllAboveThreshold = !SourceLODs.ContainsByPredicate(
				[InMinLOD](int32 LOD)
				{
					return LOD < InMinLOD;
				});

			if (!bAllAboveThreshold)
			{
				continue;
			}

			NewMaterials[SlotIndex].MaterialInterface = InMaskMaterial;
			bAnyChanged = true;
		}

		if (bAnyChanged)
		{
			InMesh->SetMaterials(NewMaterials);
		}
	}
}
