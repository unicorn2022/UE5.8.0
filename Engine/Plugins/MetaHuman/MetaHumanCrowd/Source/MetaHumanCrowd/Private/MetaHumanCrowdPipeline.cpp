// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCrowdPipeline.h"

#include "Item/MetaHumanCrowdCharacterPipeline.h"
#include "Item/MetaHumanCrowdGroomPipeline.h"
#include "Item/MetaHumanCrowdHeadPipeline.h"
#include "Item/MetaHumanCrowdOutfitPipeline.h"
#include "Item/MetaHumanMaterialPipelineCommon.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanInstance.h"
#include "MetaHumanCollection.h"
#include "MetaHumanCollectionEditorPipeline.h"
#include "MetaHumanCrowdLog.h"
#include "MetaHumanCrowdMaterialUtils.h"

#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "StructUtils/PropertyBag.h"

uint32 GetTypeHash(const FMetaHumanCrowdFaceMICComboKey& Key)
{
	uint32 Hash = GetTypeHash(Key.HeadKey);
	for (const FMetaHumanCrowdFaceMICComboSlot& Entry : Key.EquippedGrooms)
	{
		Hash = HashCombine(Hash, GetTypeHash(Entry));
	}
	return Hash;
}

namespace
{
	/** True if InSlotName is one of the groom slots this pipeline aggregates into ActorGrooms/InstancedGrooms. */
	bool IsGroomSlot(const FName InSlotName)
	{
		return InSlotName == UE::MetaHuman::CharacterPipelineSlots::Hair
			|| InSlotName == UE::MetaHuman::CharacterPipelineSlots::Eyebrows
			|| InSlotName == UE::MetaHuman::CharacterPipelineSlots::Beard
			|| InSlotName == UE::MetaHuman::CharacterPipelineSlots::Mustache;
	}

	/**
	 * Walks InFaceMesh's material slots and, for each whose name is in InWrappedSlotNames, creates
	 * a UMaterialInstanceDynamic parented to the slot's existing material. The MIDs are stashed in
	 * OutFaceMaterialOverrides keyed by face slot name. Caller is responsible for plumbing the MIDs
	 * onto the face mesh component (e.g. via the Mass material-override path).
	 */
	void CreateFaceMaterialOverrides(
		const USkeletalMesh* InFaceMesh,
		TConstArrayView<FName> InWrappedSlotNames,
		UObject* InOuter,
		TMap<FName, TObjectPtr<UMaterialInterface>>& OutFaceMaterialOverrides)
	{
		if (!InFaceMesh || InWrappedSlotNames.IsEmpty())
		{
			return;
		}

		const TArray<FSkeletalMaterial>& FaceMaterials = InFaceMesh->GetMaterials();
		for (const FSkeletalMaterial& SlotEntry : FaceMaterials)
		{
			if (!SlotEntry.MaterialInterface || !InWrappedSlotNames.Contains(SlotEntry.MaterialSlotName))
			{
				continue;
			}

			FName GeneratedName(*FString::Printf(TEXT("MID_%s"), *SlotEntry.MaterialSlotName.ToString()));
			const UMaterialInterface* ParentMaterial = SlotEntry.MaterialInterface;
			UMaterialInstanceDynamic* MID = UE::MetaHuman::MaterialUtils::CreateUniqueNamedMaterialInstanceDynamic(
				ParentMaterial,
				InOuter,
				GeneratedName);
			if (MID)
			{
				OutFaceMaterialOverrides.Add(SlotEntry.MaterialSlotName, MID);
			}
		}
	}

	/**
	 * Build the face ISKM's per-instance custom-data float buffer for this assembly.
	 *
	 * Sources color parameter values from the equipped grooms' assembly parameter bags (via
	 * GroomFaceParameterBindings), routes them through the face slot's custom-data layout
	 * (FaceSlotCustomDataLayout), and writes the result into
	 * AssemblyOutput.InstancedFaceMeshCustomDataFloats. Replaces per-MID Scalar/Vector writes
	 * that ApplyGroomFaceParameterBindings used to do for hair colour params.
	 */
	void BuildInstancedFaceCustomData(
		const UMetaHumanCollectionPipeline::FAssembleCollectionParams& Params,
		const FMetaHumanPaletteItemKey& InHeadItemKey,
		const FMetaHumanCrowdCollectionBuildOutput& InCollectionBuildOutput,
		FMetaHumanCrowdAssemblyOutput& InOutAssemblyOutput)
	{
		const TMap<FName, FMetaHumanCrowdOutfitInstancedMaterial>& SlotLayout = InCollectionBuildOutput.FaceSlotCustomDataLayout;
		if (SlotLayout.IsEmpty())
		{
			return;
		}

		const int32 BufferSize = UE::MetaHuman::MaterialUtils::ComputeISKMCustomDataSize(SlotLayout);
		if (BufferSize <= 0)
		{
			return;
		}

		TArray<float>& Buffer = InOutAssemblyOutput.InstancedFaceMeshCustomDataFloats;
		Buffer.Reset();
		Buffer.SetNumZeroed(BufferSize);

		for (const FMetaHumanCrowdGroomFaceParameterBinding& Binding : InCollectionBuildOutput.GroomFaceParameterBindings)
		{
			if (Binding.HeadItemKey != InHeadItemKey)
			{
				continue;
			}

			if (Binding.GroomItemPath.GetNumPathEntries() == 0)
			{
				continue;
			}

			// Only equipped grooms contribute. An unequipped groom doesn't write to the buffer
			// (defaults of 0 stay in place, matching what the face material reads as "no input").
			const FMetaHumanPaletteItemKey GroomLeafKey = Binding.GroomItemPath.GetPathEntry(0);
			bool bEquipped = false;
			for (const FMetaHumanPipelineSlotSelectionData& SlotSelection : Params.SlotSelections)
			{
				if (SlotSelection.Selection.SelectedItem == GroomLeafKey)
				{
					bEquipped = true;
					break;
				}
			}
			if (!bEquipped)
			{
				continue;
			}

			const FInstancedPropertyBag* AssemblyParameterBag = Params.AssemblyParameters.Find(Binding.GroomItemPath);
			if (!AssemblyParameterBag)
			{
				continue;
			}

			const UPropertyBag* PropertyBag = AssemblyParameterBag->GetPropertyBagStruct();
			if (!PropertyBag)
			{
				continue;
			}

			for (const FMetaHumanMaterialParameter& Parameter : Binding.Parameters)
			{
				const FPropertyBagPropertyDesc* PropertyDesc = PropertyBag->FindPropertyDescByName(Parameter.InstanceParameterName);
				if (!PropertyDesc)
				{
					continue;
				}

				const FName FormatLookupKey = Parameter.MaterialParameter.Name;

				for (const FName& FaceSlotName : Parameter.SlotNames)
				{
					const FMetaHumanCrowdOutfitInstancedMaterial* SlotEntry = SlotLayout.Find(FaceSlotName);
					if (!SlotEntry)
					{
						continue;
					}

					const FMetaHumanCrowdOutfitCustomDataFormat* CustomDataFormat = SlotEntry->InstanceParameterNameToCustomDataFormat.Find(FormatLookupKey);
					if (!CustomDataFormat)
					{
						continue;
					}

					switch (Parameter.ParameterType)
					{
					case EMetaHumanRuntimeMaterialParameterType::Toggle:
						{
							const TValueOrError<bool, EPropertyBagResult> Result = AssemblyParameterBag->GetValueBool(*PropertyDesc);
							if (Result.HasValue() && Buffer.IsValidIndex(CustomDataFormat->CustomDataOffset))
							{
								Buffer[CustomDataFormat->CustomDataOffset] = Result.GetValue() ? 1.0f : 0.0f;
							}
						}
						break;

					case EMetaHumanRuntimeMaterialParameterType::Scalar:
						{
							const TValueOrError<float, EPropertyBagResult> Result = AssemblyParameterBag->GetValueFloat(*PropertyDesc);
							if (Result.HasValue() && Buffer.IsValidIndex(CustomDataFormat->CustomDataOffset))
							{
								Buffer[CustomDataFormat->CustomDataOffset] = Result.GetValue();
							}
						}
						break;

					case EMetaHumanRuntimeMaterialParameterType::Vector:
						{
							const TValueOrError<FLinearColor*, EPropertyBagResult> Result = AssemblyParameterBag->GetValueStruct<FLinearColor>(*PropertyDesc);
							if (!Result.HasValue() || Result.GetValue() == nullptr)
							{
								break;
							}

							int32 Offset = CustomDataFormat->CustomDataOffset;
							for (int32 ComponentIndex = 0; ComponentIndex < 4; ComponentIndex++)
							{
								bool bChannelUsed = false;
								switch (ComponentIndex)
								{
								case 0: bChannelUsed = CustomDataFormat->bUseChannelR; break;
								case 1: bChannelUsed = CustomDataFormat->bUseChannelG; break;
								case 2: bChannelUsed = CustomDataFormat->bUseChannelB; break;
								case 3: bChannelUsed = CustomDataFormat->bUseChannelA; break;
								}

								if (!bChannelUsed)
								{
									continue;
								}

								if (Buffer.IsValidIndex(Offset))
								{
									Buffer[Offset] = Result.GetValue()->Component(ComponentIndex);
								}
								Offset++;
							}
						}
						break;

					default:
						break;
					}
				}
			}
		}
	}
}

const FName UMetaHumanCrowdPipeline::HeadSlotName = TEXT("Head");
const FName UMetaHumanCrowdPipeline::BodySlotName = TEXT("Body");
const FName UMetaHumanCrowdPipeline::OutfitsSlotName = TEXT("Outfits");
const FName UMetaHumanCrowdPipeline::TopGarmentSlotName = TEXT("Top Garment");
const FName UMetaHumanCrowdPipeline::BottomGarmentSlotName = TEXT("Bottom Garment");
const FName UMetaHumanCrowdPipeline::ShoesSlotName = TEXT("Shoes");

UMetaHumanCrowdPipeline::UMetaHumanCrowdPipeline()
{
	// Initialize the specification
	{
		Specification = CreateDefaultSubobject<UMetaHumanCharacterPipelineSpecification>("Specification", /*bTransient*/ true);
		Specification->AssemblyOutputStruct = FMetaHumanCrowdAssemblyOutput::StaticStruct();
		Specification->BuildOutputStruct = FMetaHumanCrowdCollectionBuildOutput::StaticStruct();

		{
			FMetaHumanCharacterPipelineSlot& Slot = Specification->Slots.FindOrAdd(HeadSlotName);
			Slot.BuildOutputStruct = FMetaHumanCrowdCharacterBuildOutput::StaticStruct();
			Slot.SlotColor = FLinearColor(0.9f, 0.45f, 0.25f); // Warm orange
		}

		{
			FMetaHumanCharacterPipelineSlot& Slot = Specification->Slots.FindOrAdd(BodySlotName);
			Slot.BuildOutputStruct = FMetaHumanCrowdCharacterBuildOutput::StaticStruct();
			Slot.AssemblyOutputStruct = FMetaHumanCrowdCharacterAssemblyOutput::StaticStruct();
			Slot.SlotColor = FLinearColor(0.85f, 0.7f, 0.55f); // Skin/tan
		}

		// Outfits
		{
			// Real slot that the pipeline will use
			{
				FMetaHumanCharacterPipelineSlot& Slot = Specification->Slots.FindOrAdd(OutfitsSlotName);
				Slot.AssemblyOutputStruct = FMetaHumanCrowdOutfitAssemblyOutput::StaticStruct();
				Slot.AssemblyOutputMapping = FMetaHumanAssemblyOutputMapping
				{
					.Method = EMetaHumanAssemblyOutputMappingMethod::CustomValue
				};
				Slot.BuildOutputStruct = FMetaHumanCrowdOutfitBuildOutput::StaticStruct();

				Slot.bVisibleToUser = false;
				Slot.bAllowsMultipleSelection = true;
			}

			// Virtual slots to allow single-select UI for these garments
			{
				FMetaHumanCharacterPipelineSlot& Slot = Specification->Slots.FindOrAdd(TopGarmentSlotName);
				Slot.TargetSlot = OutfitsSlotName;
				Slot.SlotColor = FLinearColor(0.3f, 0.5f, 0.85f); // Blue
			}

			{
				FMetaHumanCharacterPipelineSlot& Slot = Specification->Slots.FindOrAdd(BottomGarmentSlotName);
				Slot.TargetSlot = OutfitsSlotName;
				Slot.SlotColor = FLinearColor(0.25f, 0.7f, 0.65f); // Teal
			}

			{
				FMetaHumanCharacterPipelineSlot& Slot = Specification->Slots.FindOrAdd(ShoesSlotName);
				Slot.TargetSlot = OutfitsSlotName;
				Slot.SlotColor = FLinearColor(0.55f, 0.45f, 0.3f); // Brown
			}
		}

		// Groom
		{
			// Hair
			{
				FMetaHumanCharacterPipelineSlot& Slot = Specification->Slots.FindOrAdd(UE::MetaHuman::CharacterPipelineSlots::Hair);
				Slot.AssemblyOutputStruct = FMetaHumanCrowdGroomAssemblyOutput::StaticStruct();
				Slot.AssemblyOutputMapping = FMetaHumanAssemblyOutputMapping
				{
					.Method = EMetaHumanAssemblyOutputMappingMethod::CustomValue
				};
				Slot.SlotColor = FLinearColor(0.45f, 0.25f, 0.7f); // Purple
			}

			// Eyebrows
			{
				FMetaHumanCharacterPipelineSlot& Slot = Specification->Slots.FindOrAdd(UE::MetaHuman::CharacterPipelineSlots::Eyebrows);
				Slot.AssemblyOutputStruct = FMetaHumanCrowdGroomAssemblyOutput::StaticStruct();
				Slot.AssemblyOutputMapping = FMetaHumanAssemblyOutputMapping
				{
					.Method = EMetaHumanAssemblyOutputMappingMethod::CustomValue
				};
				Slot.SlotColor = FLinearColor(0.6f, 0.4f, 0.25f); // Brown-orange
			}
			
			// Beard
			{
				FMetaHumanCharacterPipelineSlot& Slot = Specification->Slots.FindOrAdd(UE::MetaHuman::CharacterPipelineSlots::Beard);
				Slot.AssemblyOutputStruct = FMetaHumanCrowdGroomAssemblyOutput::StaticStruct();
				Slot.AssemblyOutputMapping = FMetaHumanAssemblyOutputMapping
				{
					.Method = EMetaHumanAssemblyOutputMappingMethod::CustomValue
				};
				Slot.SlotColor = FLinearColor(0.65f, 0.35f, 0.35f); // Muted red
			}

			// Mustache
			{
				FMetaHumanCharacterPipelineSlot& Slot = Specification->Slots.FindOrAdd(UE::MetaHuman::CharacterPipelineSlots::Mustache);
				Slot.AssemblyOutputStruct = FMetaHumanCrowdGroomAssemblyOutput::StaticStruct();
				Slot.AssemblyOutputMapping = FMetaHumanAssemblyOutputMapping
				{
					.Method = EMetaHumanAssemblyOutputMappingMethod::CustomValue
				};
				Slot.SlotColor = FLinearColor(0.4f, 0.55f, 0.35f); // Olive green
			}
		}
	}
}

#if WITH_EDITOR
void UMetaHumanCrowdPipeline::SetDefaultEditorPipeline()
{
	EditorPipeline = nullptr;

	const TSoftClassPtr<UMetaHumanCollectionEditorPipeline> SoftEditorPipelineClass(FSoftObjectPath(TEXT("/Script/MetaHumanCrowdEditor.MetaHumanCrowdEditorPipeline")));
	const TSubclassOf<UMetaHumanCollectionEditorPipeline> EditorPipelineClass(SoftEditorPipelineClass.Get());
	if (EditorPipelineClass)
	{
		EditorPipeline = NewObject<UMetaHumanCollectionEditorPipeline>(this, EditorPipelineClass);
	}
}

const UMetaHumanCollectionEditorPipeline* UMetaHumanCrowdPipeline::GetEditorPipeline() const
{
	return EditorPipeline;
}

UMetaHumanCollectionEditorPipeline* UMetaHumanCrowdPipeline::GetMutableEditorPipeline()
{
	return EditorPipeline;
}
#endif // WITH_EDITOR

void UMetaHumanCrowdPipeline::AssembleCollection(const FAssembleCollectionParams& Params, const FOnAssemblyComplete& OnComplete) const
{
	if (!Params.Collection->GetBuiltData().IsValid())
	{
		OnComplete.ExecuteIfBound(FMetaHumanAssemblyOutput());
		return;
	}
	
	const FMetaHumanCollectionBuiltData& BuildOutput = Params.Collection->GetBuiltData();

	// The Collection's own build output. Defaulted if not present.
	FMetaHumanCrowdCollectionBuildOutput CollectionBuildOutput;
	{
		if (const FMetaHumanPipelineBuiltData* CollectionBuildData = BuildOutput.PaletteBuiltData.ItemBuiltData.View().Find(FMetaHumanPaletteItemPath()))
		{
			if (const FMetaHumanCrowdCollectionBuildOutput* CollectionBuildOutputPtr = CollectionBuildData->BuildOutput.GetPtr<FMetaHumanCrowdCollectionBuildOutput>())
			{
				CollectionBuildOutput = *CollectionBuildOutputPtr;
			}
		}
	}

	FMetaHumanAssemblyOutput Result;
	FMetaHumanCrowdAssemblyOutput& AssemblyOutput = Result.PipelineAssemblyOutput.InitializeAs<FMetaHumanCrowdAssemblyOutput>();

	AssemblyOutput.AnimBPAnimations = CollectionBuildOutput.SharedAnimBPAnimations;

	// Head
	FMetaHumanPaletteItemKey HeadItemKey;
	if (UMetaHumanInstance::TryGetAnySlotSelection(Params.SlotSelections, HeadSlotName, HeadItemKey))
	{
		const FMetaHumanPaletteItemPath HeadItemPath(HeadItemKey);
		const FMetaHumanPipelineBuiltData* BuildOutputForSlot = BuildOutput.PaletteBuiltData.ItemBuiltData.View().Find(HeadItemPath);

		if (BuildOutputForSlot)
		{
			if (const FMetaHumanCrowdCollectionHeadBuildOutput* HeadBuildOutput = BuildOutputForSlot->BuildOutput.GetPtr<FMetaHumanCrowdCollectionHeadBuildOutput>())
			{
				AssemblyOutput.ActorFaceMesh = HeadBuildOutput->ActorFaceMesh;
				AssemblyOutput.InstancedFaceMesh = HeadBuildOutput->InstancedFaceMesh;
				AssemblyOutput.InstancedFaceMeshTransformProvider = HeadBuildOutput->InstancedFaceMeshTransformProvider;
				AssemblyOutput.BakedAnimRootProvider = HeadBuildOutput->BakedAnimRootProvider;

				// Actor variant: always create fresh per-Assemble MIDs parented to the face
				// mesh's own slot materials.
				CreateFaceMaterialOverrides(
					AssemblyOutput.ActorFaceMesh,
					CollectionBuildOutput.FaceMaterialSlotsForRuntimeMID,
					Params.OuterForGeneratedObjects,
					AssemblyOutput.ActorFaceMaterialOverrides);

				// Look up the pre-baked face MIC set for the current (head, equipped-grooms) combo.
				FMetaHumanCrowdFaceMICComboKey ComboKey;
				ComboKey.HeadKey = HeadItemKey;
				ComboKey.EquippedGrooms.Reserve(CollectionBuildOutput.FaceAffectingPipelineSlots.Num());

				for (const FName& SlotName : CollectionBuildOutput.FaceAffectingPipelineSlots)
				{
					FMetaHumanCrowdFaceMICComboSlot& Entry = ComboKey.EquippedGrooms.AddDefaulted_GetRef();
					Entry.PipelineSlotName = SlotName;
					(void)UMetaHumanInstance::TryGetAnySlotSelection(Params.SlotSelections, SlotName, Entry.EquippedGroomKey);
				}

				// Instanced variant: assign the shared combo MIC directly.
				if (const FMetaHumanCrowdFaceMICSet* MICSet = CollectionBuildOutput.FaceMICsByCombo.Find(ComboKey))
				{
					for (const TPair<FName, TObjectPtr<UMaterialInstanceConstant>>& Pair : MICSet->InstancedSlotToMIC)
					{
						AssemblyOutput.InstancedFaceMaterialOverrides.Add(Pair.Key, Pair.Value);
					}
				}
				else
				{
					// No pre-baked combo, fall back to fresh per-Assemble MIDs
					CreateFaceMaterialOverrides(
						AssemblyOutput.InstancedFaceMesh,
						CollectionBuildOutput.FaceMaterialSlotsForRuntimeMID,
						Params.OuterForGeneratedObjects,
						AssemblyOutput.InstancedFaceMaterialOverrides);
				}

				BuildInstancedFaceCustomData(
					Params,
					HeadItemKey,
					CollectionBuildOutput,
					AssemblyOutput);
			}
		}
	}

	// Body
	FMetaHumanPaletteItemKey BodyItemKey;
	if (UMetaHumanInstance::TryGetAnySlotSelection(Params.SlotSelections, BodySlotName, BodyItemKey))
	{
		const FMetaHumanPaletteItemPath BodyItemPath(BodyItemKey);
		const FMetaHumanPipelineBuiltData* BuildOutputForSlot = BuildOutput.PaletteBuiltData.ItemBuiltData.View().Find(BodyItemPath);

		if (BuildOutputForSlot)
		{
			if (const FMetaHumanCrowdCollectionBodyBuildOutput* BodyBuildOutput = BuildOutputForSlot->BuildOutput.GetPtr<FMetaHumanCrowdCollectionBodyBuildOutput>())
			{
				AssemblyOutput.ActorBodyMesh = BodyBuildOutput->ActorBodyMesh;
				// Note that this will be nullptr if bBodyGeometryMergedOntoClothing is true
				AssemblyOutput.InstancedBodyMesh = BodyBuildOutput->InstancedBodyMesh;
				AssemblyOutput.InstancedBodyMeshTransformProvider = BodyBuildOutput->InstancedBodyMeshTransformProvider;
				AssemblyOutput.bIsBodyMeshVisible = !CollectionBuildOutput.bBodyGeometryMergedOntoClothing;
			}
		}
	}

	// Outfits
	if (!BodyItemKey.IsNull())
	{
		for (const FMetaHumanPipelineSlotSelectionData& SlotSelection : Params.SlotSelections)
		{
			if (SlotSelection.Selection.SlotName != OutfitsSlotName)
			{
				continue;
			}
		
			const FMetaHumanPaletteItemKey& OutfitItemKey = SlotSelection.Selection.SelectedItem;
			const FMetaHumanPaletteItemPath OutfitItemPath(OutfitItemKey);
			const FMetaHumanPipelineBuiltData* BuildOutputForSlot = BuildOutput.PaletteBuiltData.ItemBuiltData.View().Find(OutfitItemPath);

			if (BuildOutputForSlot)
			{
				const FMetaHumanCrowdCollectionOutfitBuildOutput* CollectionOutfitBuildOutput = BuildOutputForSlot->BuildOutput.GetPtr<FMetaHumanCrowdCollectionOutfitBuildOutput>();
				if (!CollectionOutfitBuildOutput)
				{
					continue;
				}

				const UMetaHumanItemPipeline* ItemPipeline = nullptr;
				if (!Params.Collection->TryResolveItemPipeline(OutfitItemPath, ItemPipeline))
				{
					ItemPipeline = GetDefault<UMetaHumanCrowdOutfitPipeline>();
				}

				FInstancedStruct ItemAssemblyInput;
				FMetaHumanCrowdOutfitAssemblyInput& OutfitAssemblyInput = ItemAssemblyInput.InitializeAs<FMetaHumanCrowdOutfitAssemblyInput>();
				OutfitAssemblyInput.BodyItem = BodyItemKey;

				// Reconstruct a minimal item-level build output for AssembleItem.
				//
				// The item pipeline only reads the Materials array on the geometry bundle;
				// everything else can remain empty. The final actor mesh carries the
				// authoritative slot-name-keyed materials produced by the build, so we source
				// them from there.
				const TObjectPtr<USkeletalMesh>* ActorMeshPtr = CollectionOutfitBuildOutput->BodyToActorOutfitMeshMap.Find(BodyItemKey);
				USkeletalMesh* ActorMesh = (ActorMeshPtr && *ActorMeshPtr) ? ActorMeshPtr->Get() : nullptr;
				if (!ActorMesh)
				{
					// No renderable actor mesh for this body -- nothing to assemble against.
					continue;
				}

				FMetaHumanPaletteBuiltData ItemBuiltDataForAssemble;
				FMetaHumanPipelineBuiltData& ReconstructedPipelineBuiltData = ItemBuiltDataForAssemble.ItemBuiltData.Edit().Add(OutfitItemPath);
				ReconstructedPipelineBuiltData.SlotName = BuildOutputForSlot->SlotName;
				FMetaHumanCrowdOutfitBuildOutput& ReconstructedOutput = ReconstructedPipelineBuiltData.BuildOutput.InitializeAs<FMetaHumanCrowdOutfitBuildOutput>();
				FMetaHumanCrowdMeshGeometryBundle& ReconstructedBundle = ReconstructedOutput.BodyToOutfitGeometryMap.Add(BodyItemKey);
				ReconstructedBundle.Materials = ActorMesh->GetMaterials();

				const UMetaHumanItemPipeline::FAssembleItemParams ItemParams
				{
					.BaseItemPath = OutfitItemPath,
					.OuterForGeneratedObjects = Params.OuterForGeneratedObjects,
					.ItemBuiltData = ItemBuiltDataForAssemble.ItemBuiltData.View(),
					.Quality = Params.Collection->GetQuality(),
					.AssemblyInput = ItemAssemblyInput,
					.AssemblyParameters = Params.AssemblyParameters.FilterByBasePath(OutfitItemPath)
				};

				FMetaHumanAssemblyOutput ItemAssemblyOutput;
				ItemPipeline->AssembleItemSynchronous(ItemParams, ItemAssemblyOutput);

				if (const FMetaHumanCrowdOutfitAssemblyOutput* OutfitAssemblyOutput = ItemAssemblyOutput.PipelineAssemblyOutput.GetPtr<FMetaHumanCrowdOutfitAssemblyOutput>())
				{
					// Actor clothing: mesh from collection build output, MID overrides from item assembly
					FMetaHumanCrowdActorClothingAssemblyOutput& ActorClothing = AssemblyOutput.ActorClothing.AddDefaulted_GetRef();
					ActorClothing.OutfitMesh = ActorMesh;
					ActorClothing.OverrideMaterials = OutfitAssemblyOutput->MeshComponentOverrideMaterials;

					// Instanced clothing: mesh from collection build output, custom data from item assembly
					FMetaHumanCrowdInstancedClothingAssemblyOutput& InstancedClothing = AssemblyOutput.InstancedClothing.AddDefaulted_GetRef();
					const TObjectPtr<USkeletalMesh>* InstancedMesh = CollectionOutfitBuildOutput->BodyToInstancedOutfitMeshMap.Find(BodyItemKey);
					InstancedClothing.OutfitMesh = InstancedMesh ? *InstancedMesh : nullptr;
					if (const TObjectPtr<UAnimSequenceTransformProviderData>* InstancedProvider = CollectionOutfitBuildOutput->BodyToInstancedOutfitTransformProviderMap.Find(BodyItemKey))
					{
						InstancedClothing.TransformProvider = *InstancedProvider;
					}
					InstancedClothing.InstancedMaterialData = OutfitAssemblyOutput->InstancedMaterialData;
					InstancedClothing.InstancedMeshCustomDataFloats = OutfitAssemblyOutput->InstancedMeshCustomDataFloats;
					
					Result.Metadata.Append(MoveTemp(ItemAssemblyOutput.Metadata));
					Result.PostAssemblyParameters.Edit().Append(ItemAssemblyOutput.PostAssemblyParameters.Edit());
					Result.PostAssemblyParameters.Edit().FindOrAdd(OutfitItemPath).PipelineAssemblyOutputArrayIndex = AssemblyOutput.ActorClothing.Num() - 1;
				}
			}
		}
	}

	// Hair
	auto AssembleGroomSlot = 
		[&BuildOutput, &AssemblyOutput, &Result, &Params, &HeadItemKey](const FMetaHumanPaletteItemKey& ItemKey)
		{
			const FMetaHumanPaletteItemPath GroomItemPath(ItemKey);
			const FMetaHumanPipelineBuiltData* BuildOutputForSlot = BuildOutput.PaletteBuiltData.ItemBuiltData.View().Find(GroomItemPath);

			if (BuildOutputForSlot)
			{
				const FMetaHumanCrowdCollectionGroomBuildOutput* CollectionGroomBuildOutput = BuildOutputForSlot->BuildOutput.GetPtr<FMetaHumanCrowdCollectionGroomBuildOutput>();
				if (!CollectionGroomBuildOutput)
				{
					return;
				}

				const UMetaHumanItemPipeline* ItemPipeline = nullptr;
				if (!Params.Collection->TryResolveItemPipeline(GroomItemPath, ItemPipeline))
				{
					ItemPipeline = GetDefault<UMetaHumanCrowdGroomPipeline>();
				}

				FInstancedStruct ItemAssemblyInput;
				FMetaHumanCrowdGroomAssemblyInput& GroomAssemblyInput = ItemAssemblyInput.InitializeAs<FMetaHumanCrowdGroomAssemblyInput>();
				GroomAssemblyInput.TargetItem = HeadItemKey;

				// Reconstruct a minimal item-level build output for AssembleItem. Only the Materials
				// array on the geometry bundle is read; we source it from the final actor card
				// mesh, which carries the authoritative slot-name-keyed materials produced by the
				// build.
				const TObjectPtr<USkeletalMesh>* ActorMeshPtr = CollectionGroomBuildOutput->ItemToActorGroomMeshMap.Find(HeadItemKey);
				USkeletalMesh* ActorMesh = (ActorMeshPtr && *ActorMeshPtr) ? ActorMeshPtr->Get() : nullptr;

				FMetaHumanPaletteBuiltData ItemBuiltDataForAssemble;
				FMetaHumanPipelineBuiltData& ReconstructedPipelineBuiltData = ItemBuiltDataForAssemble.ItemBuiltData.Edit().Add(GroomItemPath);
				ReconstructedPipelineBuiltData.SlotName = BuildOutputForSlot->SlotName;
				FMetaHumanCrowdGroomBuildOutput& ReconstructedOutput = ReconstructedPipelineBuiltData.BuildOutput.InitializeAs<FMetaHumanCrowdGroomBuildOutput>();
				ReconstructedOutput.PipelineSlotName = CollectionGroomBuildOutput->PipelineSlotName;

				// Surface the baked-texture entry for strands-only grooms so AssembleItem can detect
				// the strands-only case (no geometry bundle for this head, but a baked entry exists)
				// and emit a synthetic PostAssemblyParameters bag from RuntimeMaterialParameters.
				if (const FMetaHumanCrowdGroomBakedTextureEntry* BakedEntry = CollectionGroomBuildOutput->ItemToBakedGroomTexture.Find(HeadItemKey))
				{
					ReconstructedOutput.ItemToBakedGroomTexture.Add(HeadItemKey, *BakedEntry);
				}

				if (ActorMesh)
				{
					FMetaHumanCrowdMeshGeometryBundle& ReconstructedActorBundle = ReconstructedOutput.ItemToGroomGeometryMap.Add(HeadItemKey);
					ReconstructedActorBundle.Materials = ActorMesh->GetMaterials();
				}

				// Also populate the instanced variant so AssembleItem can build instanced MIDs from the ISKM's MICs
				const TObjectPtr<USkeletalMesh>* InstancedMeshPtr = CollectionGroomBuildOutput->ItemToInstancedGroomMeshMap.Find(HeadItemKey);

				if (InstancedMeshPtr && *InstancedMeshPtr)
				{
					FMetaHumanCrowdGroomMaterialOverride& ReconstructedInstancedOverride = ReconstructedOutput.InstancedMaterialOverrides.Add(HeadItemKey);
					ReconstructedInstancedOverride.Materials = InstancedMeshPtr->Get()->GetMaterials();
				}

				const UMetaHumanItemPipeline::FAssembleItemParams ItemParams
				{
					.BaseItemPath = GroomItemPath,
					.OuterForGeneratedObjects = Params.OuterForGeneratedObjects,
					.ItemBuiltData = ItemBuiltDataForAssemble.ItemBuiltData.View(),
					.Quality = Params.Collection->GetQuality(),
					.AssemblyInput = ItemAssemblyInput,
					.AssemblyParameters = Params.AssemblyParameters.FilterByBasePath(GroomItemPath)
				};

				FMetaHumanAssemblyOutput ItemAssemblyOutput;
				ItemPipeline->AssembleItemSynchronous(ItemParams, ItemAssemblyOutput);

				if (const FMetaHumanCrowdGroomAssemblyOutput* GroomAssemblyOutput = ItemAssemblyOutput.PipelineAssemblyOutput.GetPtr<FMetaHumanCrowdGroomAssemblyOutput>())
				{
					// Actor groom
					FMetaHumanCrowdActorGroomAssemblyOutput& ActorGroom = AssemblyOutput.ActorGrooms.AddDefaulted_GetRef();
					ActorGroom.CardsMesh = ActorMesh;
					ActorGroom.OverrideMaterials = GroomAssemblyOutput->OverrideMaterials;

					// Instanced groom
					FMetaHumanCrowdInstancedGroomAssemblyOutput& InstancedGroom = AssemblyOutput.InstancedGrooms.AddDefaulted_GetRef();
					const TObjectPtr<USkeletalMesh>* InstancedMesh = CollectionGroomBuildOutput->ItemToInstancedGroomMeshMap.Find(HeadItemKey);
					InstancedGroom.CardsMesh = InstancedMesh ? *InstancedMesh : nullptr;
					if (const TObjectPtr<UAnimSequenceTransformProviderData>* InstancedProvider = CollectionGroomBuildOutput->ItemToInstancedGroomTransformProviderMap.Find(HeadItemKey))
					{
						InstancedGroom.TransformProvider = *InstancedProvider;
					}
					InstancedGroom.InstancedMaterialData = GroomAssemblyOutput->InstancedMaterialData;
					InstancedGroom.InstancedMeshCustomDataFloats = GroomAssemblyOutput->InstancedMeshCustomDataFloats;
					
					Result.Metadata.Append(MoveTemp(ItemAssemblyOutput.Metadata));
					Result.PostAssemblyParameters.Edit().Append(ItemAssemblyOutput.PostAssemblyParameters.Edit());
					Result.PostAssemblyParameters.Edit().FindOrAdd(GroomItemPath).PipelineAssemblyOutputArrayIndex = AssemblyOutput.ActorGrooms.Num() - 1;
				}
			}
		};

	if (!HeadItemKey.IsNull())
	{
		FMetaHumanPaletteItemKey GroomItemKey;

		if (UMetaHumanInstance::TryGetAnySlotSelection(Params.SlotSelections, UE::MetaHuman::CharacterPipelineSlots::Hair, GroomItemKey))
		{
			AssembleGroomSlot(GroomItemKey);
		}

		if (UMetaHumanInstance::TryGetAnySlotSelection(Params.SlotSelections, UE::MetaHuman::CharacterPipelineSlots::Eyebrows, GroomItemKey))
		{
			AssembleGroomSlot(GroomItemKey);
		}

		if (UMetaHumanInstance::TryGetAnySlotSelection(Params.SlotSelections, UE::MetaHuman::CharacterPipelineSlots::Beard, GroomItemKey))
		{
			AssembleGroomSlot(GroomItemKey);
		}

		if (UMetaHumanInstance::TryGetAnySlotSelection(Params.SlotSelections, UE::MetaHuman::CharacterPipelineSlots::Mustache, GroomItemKey))
		{
			AssembleGroomSlot(GroomItemKey);
		}

		// Apply face-side parameter values to the per-instance face MIDs. For each binding emitted at
		// build time:
		//  - If the groom item is currently equipped, write the AttributeMap texture (from the groom's
		//    BakedGroomTexture) and let the existing SetInstanceParameters route fill in color values
		//    from the user-edited assembly parameter bag at SetPostAssemblyParameters time.
		//  - If the groom is not equipped, clear {PipelineSlotSlot}AttributeMap on each face MID so an
		//    earlier equip's texture doesn't leak through.
		{
			const FMetaHumanPaletteItemPath HeadItemPath(HeadItemKey);
			const FMetaHumanPipelineBuiltData* HeadBuiltData = BuildOutput.PaletteBuiltData.ItemBuiltData.View().Find(HeadItemPath);
			const FMetaHumanCrowdCollectionHeadBuildOutput* HeadBuildOutput = HeadBuiltData
				? HeadBuiltData->BuildOutput.GetPtr<FMetaHumanCrowdCollectionHeadBuildOutput>()
				: nullptr;

			if (HeadBuildOutput)
			{
				ApplyGroomFaceParameterBindings(Params, HeadItemKey, CollectionBuildOutput, *HeadBuildOutput, AssemblyOutput);
			}
		}
	}

	OnComplete.ExecuteIfBound(MoveTemp(Result));
}

void UMetaHumanCrowdPipeline::ApplyGroomFaceParameterBindings(
	const FAssembleCollectionParams& Params,
	const FMetaHumanPaletteItemKey& HeadItemKey,
	const FMetaHumanCrowdCollectionBuildOutput& CollectionBuildOutput,
	const FMetaHumanCrowdCollectionHeadBuildOutput& HeadBuildOutput,
	FMetaHumanCrowdAssemblyOutput& AssemblyOutput) const
{
	if (CollectionBuildOutput.GroomFaceParameterBindings.IsEmpty())
	{
		return;
	}
	if (AssemblyOutput.ActorFaceMaterialOverrides.IsEmpty()
		&& AssemblyOutput.InstancedFaceMaterialOverrides.IsEmpty())
	{
		// No face MIDs got created (no FaceMaterialOverrides authored on the editor pipeline).
		return;
	}

	// Resolve which grooms are currently equipped (have a slot selection in this assembly).
	// Item key matches against the leaf key of the binding's GroomItemPath.
	auto IsGroomEquipped = [&Params](const FMetaHumanPaletteItemPath& InGroomItemPath)
	{
		if (InGroomItemPath.GetNumPathEntries() == 0)
		{
			return false;
		}

		const FMetaHumanPaletteItemKey GroomLeafKey = InGroomItemPath.GetPathEntry(0);

		for (const FMetaHumanPipelineSlotSelectionData& SlotSelection : Params.SlotSelections)
		{
			if (SlotSelection.Selection.SelectedItem == GroomLeafKey)
			{
				return true;
			}
		}

		return false;
	};

	const FMetaHumanCollectionBuiltData& BuildOutput = Params.Collection->GetBuiltData();

	// First pass: resolve the final state per PipelineSlot for this head. Multiple bindings can
	// target the same pipeline slot (e.g. when the collection has duplicate palette items for the
	// same groom asset, or sub-items). Equipped bindings win -- if any binding for a slot is
	// equipped, we use its texture; otherwise the slot stays cleared. Without this, the iteration
	// order of bindings determines whether the texture sticks: a non-equipped duplicate clears
	// what an equipped one wrote earlier in the loop.
	struct FResolvedSlotState
	{
		const FMetaHumanCrowdGroomFaceParameterBinding* EquippedBinding = nullptr;
		TOptional<FMetaHumanCrowdGroomBakedTextureEntry> BakedEntry;
	};

	TMap<FName, FResolvedSlotState> ResolvedBySlot;

	for (const FMetaHumanCrowdGroomFaceParameterBinding& Binding : CollectionBuildOutput.GroomFaceParameterBindings)
	{
		if (Binding.HeadItemKey != HeadItemKey)
		{
			continue;
		}

		FResolvedSlotState& State = ResolvedBySlot.FindOrAdd(Binding.PipelineSlotName);

		if (State.EquippedBinding)
		{
			// Already resolved by an equipped binding, nothing to do.
			continue;
		}

		if (IsGroomEquipped(Binding.GroomItemPath))
		{
			State.EquippedBinding = &Binding;

			// Look up the (groom, head) baked-texture entry on the groom's collection-level
			// build output. Copy by value so a subsequent TMap mutation can't invalidate us.
			if (const FMetaHumanPipelineBuiltData* GroomBuiltData = BuildOutput.PaletteBuiltData.ItemBuiltData.View().Find(Binding.GroomItemPath))
			{
				if (const FMetaHumanCrowdCollectionGroomBuildOutput* GroomOutput = GroomBuiltData->BuildOutput.GetPtr<FMetaHumanCrowdCollectionGroomBuildOutput>())
				{
					if (const FMetaHumanCrowdGroomBakedTextureEntry* Entry = GroomOutput->ItemToBakedGroomTexture.Find(HeadItemKey))
					{
						State.BakedEntry = *Entry;
					}
				}
			}
		}
	}

	// Second pass: write to MIDs once per PipelineSlot.
	for (const TPair<FName, FResolvedSlotState>& ResolvedPair : ResolvedBySlot)
	{
		const FName PipelineSlotName = ResolvedPair.Key;
		const FResolvedSlotState& State = ResolvedPair.Value;
		const FName AttributeMapParamName(*FString::Printf(TEXT("%sAttributeMap"), *PipelineSlotName.ToString()));

		auto WriteToVariant = [&AttributeMapParamName, &State, &Params](
			TMap<FName, TObjectPtr<UMaterialInterface>>& InOutFaceMIDs,
			const TMap<FName, FMetaHumanCrowdFaceSlotLODs>& InSlotToSourceLODs)
		{
			if (InOutFaceMIDs.IsEmpty())
			{
				return;
			}

			TMap<FName, TObjectPtr<UMaterialInstanceDynamic>> MIDMap;
			MIDMap.Reserve(InOutFaceMIDs.Num());

			for (TPair<FName, TObjectPtr<UMaterialInterface>>& Pair : InOutFaceMIDs)
			{
				if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Pair.Value))
				{
					MIDMap.Add(Pair.Key, MID);
				}
			}

			// Write {PipelineSlot}AttributeMap only on face MIDs whose slot serves a face LOD that
			// satisfies the binding's AppliesAtFaceLODs (= source LODs >= MinBakedGroomLOD). Skipping
			// non-matching slots prevents the AttributeMap from overlaying onto LODs where the cards
			// mesh is the intended renderer. Null clears the override on slots that previously had it.
			static const TArray<int32> EmptyLODs;
			
			const TArray<int32>& AppliesAtFaceLODs = State.BakedEntry.IsSet()
				? State.BakedEntry.GetValue().AppliesAtFaceLODs
				: EmptyLODs;
			
			UTexture* BakedGroomTexture = State.BakedEntry.IsSet()
				? State.BakedEntry.GetValue().Texture.Get()
				: nullptr;
			
			for (TPair<FName, TObjectPtr<UMaterialInstanceDynamic>>& MIDPair : MIDMap)
			{
				const FMetaHumanCrowdFaceSlotLODs* SlotLODs = InSlotToSourceLODs.Find(MIDPair.Key);
				const bool bAnyOverlap = SlotLODs && SlotLODs->SourceLODs.ContainsByPredicate(
					[&AppliesAtFaceLODs](int32 SourceLOD)
					{
						return AppliesAtFaceLODs.Contains(SourceLOD);
					});
				
				UTexture* TextureToUse = bAnyOverlap
					? BakedGroomTexture
					: nullptr;

				MIDPair.Value->SetTextureParameterValue(AttributeMapParamName, TextureToUse);
			}

			// Apply color parameters from the equipped binding's assembly bag. Look up the
			// bag directly (not via FilterByBasePath, which strips the path prefix and breaks
			// the Find).
			if (State.EquippedBinding)
			{
				if (const FInstancedPropertyBag* AssemblyParameterBag = Params.AssemblyParameters.Find(State.EquippedBinding->GroomItemPath))
				{
					UE::MetaHuman::MaterialUtils::SetInstanceParameters(
						State.EquippedBinding->Parameters,
						MIDMap,
						UE::MetaHuman::MaterialUtils::FFetchSlotNameDelegate(),
						*AssemblyParameterBag);
				}
			}
		};

		WriteToVariant(AssemblyOutput.ActorFaceMaterialOverrides, HeadBuildOutput.ActorFaceSlotSourceLODs);
		WriteToVariant(AssemblyOutput.InstancedFaceMaterialOverrides, HeadBuildOutput.InstancedFaceSlotSourceLODs);
	}
}

void UMetaHumanCrowdPipeline::ApplyGroomFaceParametersForBag(
	const FMetaHumanCrowdCollectionBuildOutput& CollectionBuildOutput,
	const FMetaHumanPaletteItemPath& GroomItemPath,
	const FInstancedPropertyBag& ParameterBag,
	FMetaHumanCrowdAssemblyOutput& AssemblyOutput) const
{
	if (CollectionBuildOutput.GroomFaceParameterBindings.IsEmpty())
	{
		return;
	}

	auto WriteToVariant = [](const FMetaHumanCrowdGroomFaceParameterBinding& Binding,
		const FInstancedPropertyBag& Bag,
		TMap<FName, TObjectPtr<UMaterialInterface>>& InOutFaceMIDs)
	{
		if (InOutFaceMIDs.IsEmpty())
		{
			return;
		}
		TMap<FName, TObjectPtr<UMaterialInstanceDynamic>> MIDMap;
		MIDMap.Reserve(InOutFaceMIDs.Num());
		for (TPair<FName, TObjectPtr<UMaterialInterface>>& Pair : InOutFaceMIDs)
		{
			if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Pair.Value))
			{
				MIDMap.Add(Pair.Key, MID);
			}
		}
		UE::MetaHuman::MaterialUtils::SetInstanceParameters(
			Binding.Parameters,
			MIDMap,
			UE::MetaHuman::MaterialUtils::FFetchSlotNameDelegate(),
			Bag);
	};

	for (const FMetaHumanCrowdGroomFaceParameterBinding& Binding : CollectionBuildOutput.GroomFaceParameterBindings)
	{
		if (Binding.GroomItemPath != GroomItemPath)
		{
			continue;
		}

		WriteToVariant(Binding, ParameterBag, AssemblyOutput.ActorFaceMaterialOverrides);
		WriteToVariant(Binding, ParameterBag, AssemblyOutput.InstancedFaceMaterialOverrides);
	}
}

void UMetaHumanCrowdPipeline::SetPostAssemblyParameters(
	TNotNull<const UMetaHumanCollection*> Collection,
	const FMetaHumanPostAssemblyParameterOutputCollectionView& AllPostAssemblyParameters,
	const FMetaHumanPaletteItemPath& TargetItemPath,
	const FInstancedPropertyBag& ModifiedPostAssemblyParameters,
	FInstancedStruct& InOutCollectionAssemblyOutput) const
{
	Super::SetPostAssemblyParameters(
		Collection,
		AllPostAssemblyParameters,
		TargetItemPath,
		ModifiedPostAssemblyParameters,
		InOutCollectionAssemblyOutput);

	// If a groom item just changed parameters, mirror the same writes onto the face MIDs so the
	// face material's matching color parameter (HairMelanin/etc.) updates alongside the groom's
	// own MID. AttributeMap textures aren't touched here, as equip state hasn't changed since
	// the last AssembleCollection.
	if (TargetItemPath.IsEmpty())
	{
		return;
	}

	FMetaHumanCrowdAssemblyOutput* AssemblyOutput = InOutCollectionAssemblyOutput.GetMutablePtr<FMetaHumanCrowdAssemblyOutput>();
	
	if (!AssemblyOutput)
	{
		return;
	}

	const FMetaHumanCollectionBuiltData& BuildOutput = Collection->GetBuiltData();
	const FMetaHumanPipelineBuiltData* CollectionBuiltData = BuildOutput.PaletteBuiltData.ItemBuiltData.View().Find(FMetaHumanPaletteItemPath());

	if (!CollectionBuiltData)
	{
		return;
	}

	const FMetaHumanCrowdCollectionBuildOutput* CollectionBuildOutput = CollectionBuiltData->BuildOutput.GetPtr<FMetaHumanCrowdCollectionBuildOutput>();

	if (!CollectionBuildOutput)
	{
		return;
	}

	ApplyGroomFaceParametersForBag(*CollectionBuildOutput, TargetItemPath, ModifiedPostAssemblyParameters, *AssemblyOutput);
}

bool UMetaHumanCrowdPipeline::AreSlotSelectionsAllowed(
	TNotNull<const UMetaHumanCollection*> Collection,
	TArrayView<const FMetaHumanPipelineSlotSelection> SlotSelections,
	FText& OutDisallowedReason) const
{
	if (!Super::AreSlotSelectionsAllowed(Collection, SlotSelections, OutDisallowedReason))
	{
		return false;
	}

	// If a head and body are selected, find out if the body is specified in the head's CompatibleBody property
	FMetaHumanPaletteItemKey HeadItemKey;
	FMetaHumanPaletteItemKey BodyItemKey;
	if (UMetaHumanInstance::TryGetAnySlotSelection(SlotSelections, HeadSlotName, HeadItemKey)
		&& UMetaHumanInstance::TryGetAnySlotSelection(SlotSelections, BodySlotName, BodyItemKey))
	{
		if (const FMetaHumanPipelineBuiltData* BuildOutputForSlot = Collection->GetBuiltData().PaletteBuiltData.ItemBuiltData.View().Find(FMetaHumanPaletteItemPath(HeadItemKey)))
		{
			if (const FMetaHumanCrowdCollectionHeadBuildOutput* HeadBuildOutput = BuildOutputForSlot->BuildOutput.GetPtr<FMetaHumanCrowdCollectionHeadBuildOutput>())
			{
				if (!HeadBuildOutput->CompatibleBody.IsNull() && HeadBuildOutput->CompatibleBody != BodyItemKey)
				{
					// The selected head is not compatible with the selected body
					return false;
				}
			}
		}
	}

	return true;
}

const UMetaHumanItemPipeline* UMetaHumanCrowdPipeline::GetFallbackItemPipelineForAssetType(const TSoftClassPtr<UObject>& InAssetClass) const
{
	return nullptr;
}

TSubclassOf<AActor> UMetaHumanCrowdPipeline::GetActorClass() const
{
	return ActorClass;
}

FInstancedStruct UMetaHumanCrowdPipeline::GetItemAssemblyOutputValue(
	TNotNull<const UMetaHumanCollection*> Collection,
	const FMetaHumanPostAssemblyParameterOutputCollectionView& AllPostAssemblyParameters,
	const FMetaHumanPaletteItemPath& TargetItemPath,
	const FInstancedPropertyBag& ModifiedPostAssemblyParameters,
	const FInstancedStruct& InCollectionAssemblyOutput) const
{
	// This should only be called for slots marked CustomValue
	check(Collection->GetBuiltData().IsValid());
	check(InCollectionAssemblyOutput.IsValid());
	check(!TargetItemPath.IsEmpty());

	const FMetaHumanCrowdAssemblyOutput* CollectionOutput = InCollectionAssemblyOutput.GetPtr<FMetaHumanCrowdAssemblyOutput>();
	if (!CollectionOutput)
	{
		UE_LOGFMT(LogMetaHumanCrowd, Error, "GetItemAssemblyOutputValue: Incompatible collection assembly output");

		return FInstancedStruct();
	}

	const FMetaHumanPaletteItemKey BaseItemKey = TargetItemPath.GetPathEntry(0);
	const FMetaHumanPaletteItemPath BaseItemPath(BaseItemKey);

	FMetaHumanCharacterPaletteItem Item;
	// The given item path is guaranteed by the framework to reference a valid item
	verify(Collection->TryFindItem(BaseItemKey, Item));

	TNotNull<const UMetaHumanCharacterPipelineSpecification*> PipelineSpec = GetSpecification();
	const TOptional<FName> RealSlotName = PipelineSpec->ResolveRealSlotName(Item.SlotName);
	check(RealSlotName.IsSet());

	if (RealSlotName.GetValue() == OutfitsSlotName)
	{
		const FMetaHumanPostAssemblyParameterOutput& PAPOutput = AllPostAssemblyParameters[BaseItemPath];
		if (PAPOutput.PipelineAssemblyOutputArrayIndex == INDEX_NONE)
		{
			UE_LOGFMT(LogMetaHumanCrowd, Error, "GetItemAssemblyOutputValue: PipelineAssemblyOutputArrayIndex not set");

			return FInstancedStruct();
		}

		const int32 ArrayIndex = PAPOutput.PipelineAssemblyOutputArrayIndex;
		if (!CollectionOutput->ActorClothing.IsValidIndex(ArrayIndex))
		{
			UE_LOGFMT(LogMetaHumanCrowd, Error, "GetItemAssemblyOutputValue: PipelineAssemblyOutputArrayIndex out of range");

			return FInstancedStruct();
		}

		FInstancedStruct Result;
		FMetaHumanCrowdOutfitAssemblyOutput& CrowdOutfitOutput = Result.InitializeAs<FMetaHumanCrowdOutfitAssemblyOutput>();
		
		CrowdOutfitOutput.MeshComponentOverrideMaterials = CollectionOutput->ActorClothing[ArrayIndex].OverrideMaterials;
		if (CollectionOutput->InstancedClothing.IsValidIndex(ArrayIndex))
		{
			CrowdOutfitOutput.InstancedMaterialData = CollectionOutput->InstancedClothing[ArrayIndex].InstancedMaterialData;
			CrowdOutfitOutput.InstancedMeshCustomDataFloats = CollectionOutput->InstancedClothing[ArrayIndex].InstancedMeshCustomDataFloats;
		}

		return Result;
	}

	if (IsGroomSlot(RealSlotName.GetValue()))
	{
		const FMetaHumanPostAssemblyParameterOutput& PAPOutput = AllPostAssemblyParameters[BaseItemPath];
		if (PAPOutput.PipelineAssemblyOutputArrayIndex == INDEX_NONE)
		{
			UE_LOGFMT(LogMetaHumanCrowd, Error, "GetItemAssemblyOutputValue: PipelineAssemblyOutputArrayIndex not set");

			return FInstancedStruct();
		}

		const int32 ArrayIndex = PAPOutput.PipelineAssemblyOutputArrayIndex;
		if (!CollectionOutput->ActorGrooms.IsValidIndex(ArrayIndex))
		{
			UE_LOGFMT(LogMetaHumanCrowd, Error, "GetItemAssemblyOutputValue: PipelineAssemblyOutputArrayIndex out of range");

			return FInstancedStruct();
		}

		FInstancedStruct Result;
		FMetaHumanCrowdGroomAssemblyOutput& CrowdGroomOutput = Result.InitializeAs<FMetaHumanCrowdGroomAssemblyOutput>();

		CrowdGroomOutput.OverrideMaterials = CollectionOutput->ActorGrooms[ArrayIndex].OverrideMaterials;
		if (CollectionOutput->InstancedGrooms.IsValidIndex(ArrayIndex))
		{
			CrowdGroomOutput.InstancedMaterialData = CollectionOutput->InstancedGrooms[ArrayIndex].InstancedMaterialData;
			CrowdGroomOutput.InstancedMeshCustomDataFloats = CollectionOutput->InstancedGrooms[ArrayIndex].InstancedMeshCustomDataFloats;
		}

		return Result;
	}

	// Unhandled slot
	checkNoEntry();
	return FInstancedStruct();
}
	
void UMetaHumanCrowdPipeline::SetItemAssemblyOutputValue(
	TNotNull<const UMetaHumanCollection*> Collection,
	const FMetaHumanPostAssemblyParameterOutputCollectionView& AllPostAssemblyParameters,
	const FMetaHumanPaletteItemPath& TargetItemPath,
	const FInstancedPropertyBag& ModifiedPostAssemblyParameters,
	FInstancedStruct&& ItemAssemblyOutput,
	FInstancedStruct& InOutCollectionAssemblyOutput) const
{
	// This should only be called for slots marked CustomValue
	check(Collection->GetBuiltData().IsValid());
	check(InOutCollectionAssemblyOutput.IsValid());
	check(!TargetItemPath.IsEmpty());

	FMetaHumanCrowdAssemblyOutput* CollectionOutput = InOutCollectionAssemblyOutput.GetMutablePtr<FMetaHumanCrowdAssemblyOutput>();
	if (!CollectionOutput)
	{
		UE_LOGFMT(LogMetaHumanCrowd, Error, "SetItemAssemblyOutputValue: Incompatible collection assembly output");
		return;
	}

	const FMetaHumanPaletteItemKey BaseItemKey = TargetItemPath.GetPathEntry(0);
	const FMetaHumanPaletteItemPath BaseItemPath(BaseItemKey);

	FMetaHumanCharacterPaletteItem Item;
	// The given item path is guaranteed by the framework to reference a valid item
	verify(Collection->TryFindItem(BaseItemKey, Item));

	TNotNull<const UMetaHumanCharacterPipelineSpecification*> PipelineSpec = GetSpecification();
	const TOptional<FName> RealSlotName = PipelineSpec->ResolveRealSlotName(Item.SlotName);
	check(RealSlotName.IsSet());

	if (RealSlotName.GetValue() == OutfitsSlotName)
	{
		const FMetaHumanPostAssemblyParameterOutput& PAPOutput = AllPostAssemblyParameters[BaseItemPath];
		check(PAPOutput.PipelineAssemblyOutputArrayIndex != INDEX_NONE);
		const int32 ArrayIndex = PAPOutput.PipelineAssemblyOutputArrayIndex;
		check(CollectionOutput->ActorClothing.IsValidIndex(ArrayIndex));

		FMetaHumanCrowdOutfitAssemblyOutput& CrowdOutfitOutput = ItemAssemblyOutput.GetMutable<FMetaHumanCrowdOutfitAssemblyOutput>();
		
		// Update actor clothing material overrides
		CollectionOutput->ActorClothing[ArrayIndex].OverrideMaterials = MoveTemp(CrowdOutfitOutput.MeshComponentOverrideMaterials);

		// Update instanced clothing material data
		if (CollectionOutput->InstancedClothing.IsValidIndex(ArrayIndex))
		{
			CollectionOutput->InstancedClothing[ArrayIndex].InstancedMaterialData = MoveTemp(CrowdOutfitOutput.InstancedMaterialData);
			CollectionOutput->InstancedClothing[ArrayIndex].InstancedMeshCustomDataFloats = MoveTemp(CrowdOutfitOutput.InstancedMeshCustomDataFloats);
		}
	}
	else if (IsGroomSlot(RealSlotName.GetValue()))
	{
		const FMetaHumanPostAssemblyParameterOutput& PAPOutput = AllPostAssemblyParameters[BaseItemPath];
		check(PAPOutput.PipelineAssemblyOutputArrayIndex != INDEX_NONE);
		const int32 ArrayIndex = PAPOutput.PipelineAssemblyOutputArrayIndex;
		check(CollectionOutput->ActorGrooms.IsValidIndex(ArrayIndex));

		FMetaHumanCrowdGroomAssemblyOutput& CrowdGroomOutput = ItemAssemblyOutput.GetMutable<FMetaHumanCrowdGroomAssemblyOutput>();

		// Update actor groom material overrides
		CollectionOutput->ActorGrooms[ArrayIndex].OverrideMaterials = MoveTemp(CrowdGroomOutput.OverrideMaterials);

		// Update instanced groom material data
		if (CollectionOutput->InstancedGrooms.IsValidIndex(ArrayIndex))
		{
			CollectionOutput->InstancedGrooms[ArrayIndex].InstancedMaterialData = MoveTemp(CrowdGroomOutput.InstancedMaterialData);
			CollectionOutput->InstancedGrooms[ArrayIndex].InstancedMeshCustomDataFloats = MoveTemp(CrowdGroomOutput.InstancedMeshCustomDataFloats);
		}
	}
	else
	{
		// Unhandled slot
		checkNoEntry();
	}
}
