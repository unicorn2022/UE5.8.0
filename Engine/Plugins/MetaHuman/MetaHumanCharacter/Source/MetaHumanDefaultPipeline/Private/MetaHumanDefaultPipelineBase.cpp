// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanDefaultPipelineBase.h"

#include "Item/MetaHumanGroomPipeline.h"
#include "Item/MetaHumanSkeletalMeshPipeline.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanInstance.h"
#include "MetaHumanCharacterPipelineSpecification.h"
#include "MetaHumanCollection.h"

#include "Algo/Transform.h"

#define LOCTEXT_NAMESPACE "MetaHumanDefaultPipelineBase"

const FName UMetaHumanDefaultPipelineBase::OutfitsSlotName = "Outfits";
const FName UMetaHumanDefaultPipelineBase::TopGarmentSlotName = "Top Garment";
const FName UMetaHumanDefaultPipelineBase::BottomGarmentSlotName = "Bottom Garment";
const FName UMetaHumanDefaultPipelineBase::SkeletalMeshSlotName = "SkeletalMesh";

UMetaHumanDefaultPipelineBase::UMetaHumanDefaultPipelineBase()
{
	// Initialize the specification
	{
		Specification = CreateDefaultSubobject<UMetaHumanCharacterPipelineSpecification>("Specification", /*bTransient*/ true);
		Specification->AssemblyOutputStruct = FMetaHumanDefaultAssemblyOutput::StaticStruct();

		// Grooms
		{
			{
				FMetaHumanCharacterPipelineSlot& Slot = Specification->Slots.FindOrAdd(UE::MetaHuman::CharacterPipelineSlots::Hair);
				Slot.BuildOutputStruct = FMetaHumanGroomPipelineBuildOutput::StaticStruct();
				Slot.AssemblyInputStruct = FMetaHumanGroomPipelineAssemblyInput::StaticStruct();
				Slot.AssemblyOutputStruct = FMetaHumanGroomPipelineAssemblyOutput::StaticStruct();
				Slot.AssemblyOutputMapping = FMetaHumanAssemblyOutputMapping
				{
					.Method = EMetaHumanAssemblyOutputMappingMethod::DirectProperty,
					.PipelineOutputPropertyName = GET_MEMBER_NAME_CHECKED(FMetaHumanDefaultAssemblyOutput, Hair)
				};
				Slot.SlotColor = FLinearColor(0.45f, 0.25f, 0.7f); // Purple
			}

			{
				FMetaHumanCharacterPipelineSlot& Slot = Specification->Slots.FindOrAdd(UE::MetaHuman::CharacterPipelineSlots::Eyebrows);
				Slot.BuildOutputStruct = FMetaHumanGroomPipelineBuildOutput::StaticStruct();
				Slot.AssemblyInputStruct = FMetaHumanGroomPipelineAssemblyInput::StaticStruct();
				Slot.AssemblyOutputStruct = FMetaHumanGroomPipelineAssemblyOutput::StaticStruct();
				Slot.AssemblyOutputMapping = FMetaHumanAssemblyOutputMapping
				{
					.Method = EMetaHumanAssemblyOutputMappingMethod::DirectProperty,
					.PipelineOutputPropertyName = GET_MEMBER_NAME_CHECKED(FMetaHumanDefaultAssemblyOutput, Eyebrows)
				};
				Slot.SlotColor = FLinearColor(0.6f, 0.4f, 0.25f); // Brown-orange
			}

			{
				FMetaHumanCharacterPipelineSlot& Slot = Specification->Slots.FindOrAdd(UE::MetaHuman::CharacterPipelineSlots::Beard);
				Slot.BuildOutputStruct = FMetaHumanGroomPipelineBuildOutput::StaticStruct();
				Slot.AssemblyInputStruct = FMetaHumanGroomPipelineAssemblyInput::StaticStruct();
				Slot.AssemblyOutputStruct = FMetaHumanGroomPipelineAssemblyOutput::StaticStruct();
				Slot.AssemblyOutputMapping = FMetaHumanAssemblyOutputMapping
				{
					.Method = EMetaHumanAssemblyOutputMappingMethod::DirectProperty,
					.PipelineOutputPropertyName = GET_MEMBER_NAME_CHECKED(FMetaHumanDefaultAssemblyOutput, Beard)
				};
				Slot.SlotColor = FLinearColor(0.65f, 0.35f, 0.35f); // Muted red
			}
		
			{
				FMetaHumanCharacterPipelineSlot& Slot = Specification->Slots.FindOrAdd(UE::MetaHuman::CharacterPipelineSlots::Mustache);
				Slot.BuildOutputStruct = FMetaHumanGroomPipelineBuildOutput::StaticStruct();
				Slot.AssemblyInputStruct = FMetaHumanGroomPipelineAssemblyInput::StaticStruct();
				Slot.AssemblyOutputStruct = FMetaHumanGroomPipelineAssemblyOutput::StaticStruct();
				Slot.AssemblyOutputMapping = FMetaHumanAssemblyOutputMapping
				{
					.Method = EMetaHumanAssemblyOutputMappingMethod::DirectProperty,
					.PipelineOutputPropertyName = GET_MEMBER_NAME_CHECKED(FMetaHumanDefaultAssemblyOutput, Mustache)
				};
				Slot.SlotColor = FLinearColor(0.4f, 0.55f, 0.35f); // Olive green
			}
		
			{
				FMetaHumanCharacterPipelineSlot& Slot = Specification->Slots.FindOrAdd(UE::MetaHuman::CharacterPipelineSlots::Eyelashes);
				Slot.BuildOutputStruct = FMetaHumanGroomPipelineBuildOutput::StaticStruct();
				Slot.AssemblyInputStruct = FMetaHumanGroomPipelineAssemblyInput::StaticStruct();
				Slot.AssemblyOutputStruct = FMetaHumanGroomPipelineAssemblyOutput::StaticStruct();
				Slot.AssemblyOutputMapping = FMetaHumanAssemblyOutputMapping
				{
					.Method = EMetaHumanAssemblyOutputMappingMethod::DirectProperty,
					.PipelineOutputPropertyName = GET_MEMBER_NAME_CHECKED(FMetaHumanDefaultAssemblyOutput, Eyelashes)
				};
				Slot.SlotColor = FLinearColor(0.5f, 0.5f, 0.5f); // Grey
			}
				
			{
				FMetaHumanCharacterPipelineSlot& Slot = Specification->Slots.FindOrAdd(UE::MetaHuman::CharacterPipelineSlots::Peachfuzz);
				Slot.BuildOutputStruct = FMetaHumanGroomPipelineBuildOutput::StaticStruct();
				Slot.AssemblyInputStruct = FMetaHumanGroomPipelineAssemblyInput::StaticStruct();
				Slot.AssemblyOutputStruct = FMetaHumanGroomPipelineAssemblyOutput::StaticStruct();
				Slot.AssemblyOutputMapping = FMetaHumanAssemblyOutputMapping
				{
					.Method = EMetaHumanAssemblyOutputMappingMethod::DirectProperty,
					.PipelineOutputPropertyName = GET_MEMBER_NAME_CHECKED(FMetaHumanDefaultAssemblyOutput, Peachfuzz)
				};
				Slot.SlotColor = FLinearColor(0.85f, 0.75f, 0.6f); // Peach
			}
		}

		// Outfits
		{
			{
				FMetaHumanCharacterPipelineSlot& Slot = Specification->Slots.FindOrAdd(OutfitsSlotName);
				Slot.BuildOutputStruct = FMetaHumanOutfitPipelineBuildOutput::StaticStruct();
				Slot.AssemblyInputStruct = FMetaHumanOutfitPipelineAssemblyInput::StaticStruct();
				Slot.AssemblyOutputStruct = FMetaHumanOutfitPipelineAssemblyOutput::StaticStruct();
				Slot.AssemblyOutputMapping = FMetaHumanAssemblyOutputMapping
				{
					.Method = EMetaHumanAssemblyOutputMappingMethod::ArrayProperty,
					.PipelineOutputPropertyName = GET_MEMBER_NAME_CHECKED(FMetaHumanDefaultAssemblyOutput, ClothData)
				};
				// This is hidden for now, since the UI doesn't support multi-select. We may expose it later.
				Slot.bVisibleToUser = false;
				Slot.bAllowsMultipleSelection = true;
			}

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
		}

		// Skeletal meshes
		{
			{
				FMetaHumanCharacterPipelineSlot& Slot = Specification->Slots.FindOrAdd(SkeletalMeshSlotName);
				Slot.BuildOutputStruct = FMetaHumanSkeletalMeshPipelineBuildOutput::StaticStruct();
				Slot.AssemblyOutputStruct = FMetaHumanSkeletalMeshPipelineAssemblyOutput::StaticStruct();
				Slot.AssemblyOutputMapping = FMetaHumanAssemblyOutputMapping
				{
					.Method = EMetaHumanAssemblyOutputMappingMethod::ArrayProperty,
					.PipelineOutputPropertyName = GET_MEMBER_NAME_CHECKED(FMetaHumanDefaultAssemblyOutput, SkeletalMeshData)
				};
				Slot.bAllowsMultipleSelection = true;
			}
		}

		// Character
		{
			FMetaHumanCharacterPipelineSlot& Slot = Specification->Slots.FindOrAdd(UE::MetaHuman::CharacterPipelineSlots::Character);
			Slot.SlotColor = FLinearColor(0.9f, 0.45f, 0.25f); // Warm orange
		}
	}
}

void UMetaHumanDefaultPipelineBase::AssembleCollection(const FAssembleCollectionParams& Params, const FOnAssemblyComplete& OnComplete) const
{
	if (!Params.Collection->GetBuiltData().IsValid())
	{
		OnComplete.ExecuteIfBound(FMetaHumanAssemblyOutput());
		return;
	}

	const FMetaHumanCollectionBuiltData& BuildOutput = Params.Collection->GetBuiltData();

	FMetaHumanAssemblyOutput AssemblyOutput;
	FMetaHumanDefaultAssemblyOutput& AssemblyStruct = AssemblyOutput.PipelineAssemblyOutput.InitializeAs<FMetaHumanDefaultAssemblyOutput>();

	// Character slot
	FMetaHumanPaletteItemKey SelectedCharacterItem;
	{
		const FName SlotName = UE::MetaHuman::CharacterPipelineSlots::Character;
		
		if (UMetaHumanInstance::TryGetAnySlotSelection(Params.SlotSelections, SlotName, SelectedCharacterItem))
		{
			const FMetaHumanPipelineBuiltData* BuildOutputForSlot = BuildOutput.PaletteBuiltData.ItemBuiltData.View().Find(FMetaHumanPaletteItemPath(SelectedCharacterItem));

			if (BuildOutputForSlot
				&& BuildOutputForSlot->BuildOutput.GetPtr<FMetaHumanCharacterPartOutput>())
			{
				const FMetaHumanCharacterPartOutput& PartOutput = BuildOutputForSlot->BuildOutput.Get<FMetaHumanCharacterPartOutput>();

				AssemblyStruct.FaceMesh = PartOutput.GeneratedAssets.FaceMesh;
				AssemblyStruct.BodyMesh = PartOutput.GeneratedAssets.BodyMesh;
				AssemblyOutput.Metadata.Append(PartOutput.GeneratedAssets.Metadata);
			}
		}
	}

	// Same as AssembleMeshPart but for grooms
	auto AssembleGroomPart = [&Params, &BuildOutput, &AssemblyStruct, &AssemblyOutput](
		const FName SlotName,
		FMetaHumanGroomPipelineAssemblyOutput FMetaHumanDefaultAssemblyOutput::* AssemblyOutputMember)
	{
		// Don't call this if there's no FaceMesh
		check(AssemblyStruct.FaceMesh);

		FMetaHumanPaletteItemKey ItemKey;
		if (UMetaHumanInstance::TryGetAnySlotSelection(Params.SlotSelections, SlotName, ItemKey))
		{
			const FMetaHumanPaletteItemPath ItemPath(ItemKey);
			if (BuildOutput.PaletteBuiltData.ItemBuiltData.View().Contains(ItemPath))
			{
				const UMetaHumanItemPipeline* ItemPipeline = nullptr;
				if (!Params.Collection->TryResolveItemPipeline(ItemPath, ItemPipeline))
				{
					ItemPipeline = GetDefault<UMetaHumanGroomPipeline>();
				}

				FInstancedStruct ItemAssemblyInput;
				FMetaHumanGroomPipelineAssemblyInput& GroomAssemblyInput = ItemAssemblyInput.InitializeAs<FMetaHumanGroomPipelineAssemblyInput>();
				GroomAssemblyInput.TargetMesh = AssemblyStruct.FaceMesh;

				// TODO: Check that slot and item struct types match

				const UMetaHumanItemPipeline::FAssembleItemParams ItemParams
				{
					.BaseItemPath = ItemPath,
					.OuterForGeneratedObjects = Params.OuterForGeneratedObjects,
					.ItemBuiltData = BuildOutput.PaletteBuiltData.ItemBuiltData.View().FilterByBasePath(ItemPath),
					.Quality = Params.Collection->GetQuality(),
					.AssemblyInput = ItemAssemblyInput,
					.AssemblyParameters = Params.AssemblyParameters.FilterByBasePath(ItemPath)
				};

				FMetaHumanAssemblyOutput ItemAssemblyOutput;
				ItemPipeline->AssembleItemSynchronous(ItemParams, ItemAssemblyOutput);

				if (const FMetaHumanGroomPipelineAssemblyOutput* GroomAssemblyOutput = ItemAssemblyOutput.PipelineAssemblyOutput.GetPtr<FMetaHumanGroomPipelineAssemblyOutput>())
				{
					AssemblyStruct.*AssemblyOutputMember = *GroomAssemblyOutput;
					
					AssemblyOutput.Metadata.Append(MoveTemp(ItemAssemblyOutput.Metadata));
					AssemblyOutput.PostAssemblyParameters.Edit().Append(ItemAssemblyOutput.PostAssemblyParameters.Edit());

					// Support legacy item pipelines for now
					PRAGMA_DISABLE_DEPRECATION_WARNINGS
					AssemblyOutput.InstanceParameters.Append(MoveTemp(ItemAssemblyOutput.InstanceParameters));
					PRAGMA_ENABLE_DEPRECATION_WARNINGS
				}
			}
		}
	};

	if (AssemblyStruct.FaceMesh)
	{
		AssembleGroomPart(UE::MetaHuman::CharacterPipelineSlots::Hair, &FMetaHumanDefaultAssemblyOutput::Hair);
		AssembleGroomPart(UE::MetaHuman::CharacterPipelineSlots::Eyebrows, &FMetaHumanDefaultAssemblyOutput::Eyebrows);
		AssembleGroomPart(UE::MetaHuman::CharacterPipelineSlots::Beard, &FMetaHumanDefaultAssemblyOutput::Beard);
		AssembleGroomPart(UE::MetaHuman::CharacterPipelineSlots::Mustache, &FMetaHumanDefaultAssemblyOutput::Mustache);
		AssembleGroomPart(UE::MetaHuman::CharacterPipelineSlots::Eyelashes, &FMetaHumanDefaultAssemblyOutput::Eyelashes);
		AssembleGroomPart(UE::MetaHuman::CharacterPipelineSlots::Peachfuzz, &FMetaHumanDefaultAssemblyOutput::Peachfuzz);
	}

	// Finds all item paths for the given slot name
	auto GetItemKeys = [this, &SlotSelections = Params.SlotSelections](const FName& SlotName)
	{
		TArray<FMetaHumanPaletteItemKey> ItemKeys;

		if (const FMetaHumanCharacterPipelineSlot* FoundSlot = Specification->Slots.Find(SlotName))
		{
			if (FoundSlot->bAllowsMultipleSelection)
			{
				Algo::TransformIf(
					SlotSelections,
					ItemKeys,
					[SlotName](const FMetaHumanPipelineSlotSelectionData& Selection)
					{
						return Selection.Selection.SlotName == SlotName;
					},
					[](const FMetaHumanPipelineSlotSelectionData& Selection)
					{
						return Selection.Selection.SelectedItem;
					});
			}
			else
			{
				FMetaHumanPaletteItemKey ItemKey;

				if (UMetaHumanInstance::TryGetAnySlotSelection(SlotSelections, SlotName, ItemKey))
				{
					ItemKeys.Add(ItemKey);
				}
			}
		}
		return ItemKeys;
	};

	// Handle Outfits slot
	{
		const TArray<FMetaHumanPaletteItemKey> ItemKeys = GetItemKeys(OutfitsSlotName);

		for (const FMetaHumanPaletteItemKey& ItemKey : ItemKeys)
		{
			const FMetaHumanPaletteItemPath ItemPath(ItemKey);

			const FMetaHumanPipelineBuiltData* BuildOutputForSlot = BuildOutput.PaletteBuiltData.ItemBuiltData.View().Find(ItemPath);

			if (BuildOutputForSlot
				&& BuildOutputForSlot->BuildOutput.GetPtr<FMetaHumanOutfitPipelineBuildOutput>())
			{
				const UMetaHumanItemPipeline* ItemPipeline = nullptr;
				if (!Params.Collection->TryResolveItemPipeline(ItemPath, ItemPipeline))
				{
					ItemPipeline = GetDefault<UMetaHumanOutfitPipeline>();
				}

				FInstancedStruct ItemAssemblyInput;
				FMetaHumanOutfitPipelineAssemblyInput& OutfitAssemblyInput = ItemAssemblyInput.InitializeAs<FMetaHumanOutfitPipelineAssemblyInput>();
				OutfitAssemblyInput.SelectedCharacter = SelectedCharacterItem;

				// TODO: Check that slot and item struct types match

				const UMetaHumanItemPipeline::FAssembleItemParams ItemParams
				{
					.BaseItemPath = ItemPath,
					.OuterForGeneratedObjects = Params.OuterForGeneratedObjects,
					.ItemBuiltData = BuildOutput.PaletteBuiltData.ItemBuiltData.View().FilterByBasePath(ItemPath),
					.Quality = Params.Collection->GetQuality(),
					.AssemblyInput = ItemAssemblyInput,
					.AssemblyParameters = Params.AssemblyParameters.FilterByBasePath(ItemPath)
				};

				FMetaHumanAssemblyOutput ItemAssemblyOutput;
				ItemPipeline->AssembleItemSynchronous(ItemParams, ItemAssemblyOutput);

				if (const FMetaHumanOutfitPipelineAssemblyOutput* OutfitAssemblyOutput = ItemAssemblyOutput.PipelineAssemblyOutput.GetPtr<FMetaHumanOutfitPipelineAssemblyOutput>())
				{
					const int32 ClothDataIndex = AssemblyStruct.ClothData.Add(*OutfitAssemblyOutput);

					AssemblyOutput.Metadata.Append(MoveTemp(ItemAssemblyOutput.Metadata));
					AssemblyOutput.PostAssemblyParameters.Edit().Append(ItemAssemblyOutput.PostAssemblyParameters.Edit());
					AssemblyOutput.PostAssemblyParameters.Edit().FindOrAdd(ItemPath).PipelineAssemblyOutputArrayIndex = ClothDataIndex;

					// Support legacy item pipelines for now
					PRAGMA_DISABLE_DEPRECATION_WARNINGS
					AssemblyOutput.InstanceParameters.Append(MoveTemp(ItemAssemblyOutput.InstanceParameters));
					PRAGMA_ENABLE_DEPRECATION_WARNINGS
				}
			}
		}
	}

	// Assemble Skeletal Mesh clothing
	{
		const TArray<FMetaHumanPaletteItemKey> ItemKeys = GetItemKeys(SkeletalMeshSlotName);

		for (const FMetaHumanPaletteItemKey& ItemKey : ItemKeys)
		{
			const FMetaHumanPaletteItemPath ItemPath(ItemKey);
			
			const FMetaHumanPipelineBuiltData* BuildOutputForSlot = BuildOutput.PaletteBuiltData.ItemBuiltData.View().Find(ItemPath);

			if (!BuildOutputForSlot)
			{
				continue;
			}

			if (const FMetaHumanSkeletalMeshPipelineBuildOutput* MeshBuildOutput = BuildOutputForSlot->BuildOutput.GetPtr<FMetaHumanSkeletalMeshPipelineBuildOutput>())
			{
				if (!MeshBuildOutput->Mesh)
				{
					continue;
				}

				const UMetaHumanItemPipeline* ItemPipeline = nullptr;
				if (!Params.Collection->TryResolveItemPipeline(ItemPath, ItemPipeline))
				{
					ItemPipeline = GetDefault<UMetaHumanSkeletalMeshPipeline>();
				}

				// TODO: Check that slot and item struct types match

				const UMetaHumanItemPipeline::FAssembleItemParams ItemParams
				{
					.BaseItemPath = ItemPath,
					.OuterForGeneratedObjects = Params.OuterForGeneratedObjects,
					.ItemBuiltData = BuildOutput.PaletteBuiltData.ItemBuiltData.View().FilterByBasePath(ItemPath),
					.Quality = Params.Collection->GetQuality(),
					.AssemblyParameters = Params.AssemblyParameters.FilterByBasePath(ItemPath)
				};

				FMetaHumanAssemblyOutput ItemAssemblyOutput;
				ItemPipeline->AssembleItemSynchronous(ItemParams, ItemAssemblyOutput);

				if (const FMetaHumanSkeletalMeshPipelineAssemblyOutput* SkeletalMeshAssemblyOutput = ItemAssemblyOutput.PipelineAssemblyOutput.GetPtr<FMetaHumanSkeletalMeshPipelineAssemblyOutput>())
				{
					const int32 SkeletalMeshDataIndex = AssemblyStruct.SkeletalMeshData.Add(*SkeletalMeshAssemblyOutput);

					AssemblyOutput.Metadata.Append(MoveTemp(ItemAssemblyOutput.Metadata));
					AssemblyOutput.PostAssemblyParameters.Edit().Append(ItemAssemblyOutput.PostAssemblyParameters.Edit());
					AssemblyOutput.PostAssemblyParameters.Edit().FindOrAdd(ItemPath).PipelineAssemblyOutputArrayIndex = SkeletalMeshDataIndex;

					// Support legacy item pipelines for now
					PRAGMA_DISABLE_DEPRECATION_WARNINGS
					AssemblyOutput.InstanceParameters.Append(MoveTemp(ItemAssemblyOutput.InstanceParameters));
					PRAGMA_ENABLE_DEPRECATION_WARNINGS
				}
			}
		}
	}

	OnComplete.ExecuteIfBound(MoveTemp(AssemblyOutput));
}

const UMetaHumanItemPipeline* UMetaHumanDefaultPipelineBase::GetFallbackItemPipelineForAssetType(const TSoftClassPtr<UObject>& InAssetClass) const
{
	if (const TSubclassOf<UMetaHumanItemPipeline>* FoundPipelineClass = DefaultAssetPipelines.Find(InAssetClass))
	{
		if (*FoundPipelineClass)
		{
			return Cast<UMetaHumanItemPipeline>(FoundPipelineClass->GetDefaultObject());
		}
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE
