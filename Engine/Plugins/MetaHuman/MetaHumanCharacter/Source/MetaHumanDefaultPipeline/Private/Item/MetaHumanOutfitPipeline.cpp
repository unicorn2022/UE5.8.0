// Copyright Epic Games, Inc. All Rights Reserved.

#include "Item/MetaHumanOutfitPipeline.h"

#include "MetaHumanDefaultPipelineLog.h"
#include "MetaHumanItemEditorPipeline.h"
#include "MetaHumanDefaultPipelineBase.h"

#include "ChaosClothAsset/ClothAssetBase.h"
#include "ChaosClothAsset/ClothComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/SkeletalMesh.h"

#include "Logging/StructuredLog.h"

UMetaHumanOutfitPipeline::UMetaHumanOutfitPipeline()
{
	// Initialize the specification
	{
		Specification = CreateDefaultSubobject<UMetaHumanCharacterPipelineSpecification>("Specification", /*bTransient*/ true);

		Specification->BuildOutputStruct = FMetaHumanOutfitPipelineBuildOutput::StaticStruct();
		Specification->AssemblyInputStruct = FMetaHumanOutfitPipelineAssemblyInput::StaticStruct();
		Specification->AssemblyOutputStruct = FMetaHumanOutfitPipelineAssemblyOutput::StaticStruct();
	}
}

#if WITH_EDITOR
void UMetaHumanOutfitPipeline::SetDefaultEditorPipeline()
{
	EditorPipeline = nullptr;

	const TSubclassOf<UMetaHumanItemEditorPipeline> EditorPipelineClass = GetEditorPipelineClass();
	if (EditorPipelineClass)
	{
		EditorPipeline = NewObject<UMetaHumanItemEditorPipeline>(this, EditorPipelineClass);
	}
}

const UMetaHumanItemEditorPipeline* UMetaHumanOutfitPipeline::GetEditorPipeline() const
{
	// If there's no editor pipeline instance, we can use the Class Default Object, because 
	// pipelines are stateless and won't be modified when used.
	//
	// This is unfortunately a slow path, as it involves looking the class up by name. We could
	// cache this if it becomes a performance issue.
	if (!EditorPipeline)
	{
		const TSubclassOf<UMetaHumanItemEditorPipeline> EditorPipelineClass = GetEditorPipelineClass();
		if (EditorPipelineClass)
		{
			return EditorPipelineClass.GetDefaultObject();
		}
	}

	return EditorPipeline;
}

TSubclassOf<UMetaHumanItemEditorPipeline> UMetaHumanOutfitPipeline::GetEditorPipelineClass() const
{
	const TSoftClassPtr<UMetaHumanItemEditorPipeline> SoftEditorPipelineClass(FSoftObjectPath(TEXT("/Script/MetaHumanDefaultEditorPipeline.MetaHumanOutfitEditorPipeline")));
	
	return SoftEditorPipelineClass.Get();
}
#endif

void UMetaHumanOutfitPipeline::AssembleItem(const FAssembleItemParams& Params, const FOnAssemblyComplete& OnComplete) const
{
	const FInstancedStruct& BuildOutput = Params.ItemBuiltData[Params.BaseItemPath].BuildOutput;

	if (!BuildOutput.GetPtr<FMetaHumanOutfitPipelineBuildOutput>())
	{
		UE_LOGFMT(LogMetaHumanDefaultPipeline, Error, "Build output not provided to Outfit pipeline during assembly");
		
		OnComplete.ExecuteIfBound(FMetaHumanAssemblyOutput());
		return;
	}

	if (!Params.AssemblyInput.GetPtr<FMetaHumanOutfitPipelineAssemblyInput>())
	{
		UE_LOGFMT(LogMetaHumanDefaultPipeline, Error, "Assembly input not provided to Outfit pipeline during assembly");
		
		OnComplete.ExecuteIfBound(FMetaHumanAssemblyOutput());
		return;
	}

	const FMetaHumanOutfitPipelineAssemblyInput& OutfitAssemblyInput = Params.AssemblyInput.Get<FMetaHumanOutfitPipelineAssemblyInput>();
	const FMetaHumanOutfitGeneratedAssets* SelectedCharacterOutfit = BuildOutput.Get<FMetaHumanOutfitPipelineBuildOutput>().CharacterAssets.Find(OutfitAssemblyInput.SelectedCharacter);

	if (!SelectedCharacterOutfit)
	{
		UE_LOGFMT(LogMetaHumanDefaultPipeline, Error, "Selected character {Character} not found in Outfit pipeline build output", OutfitAssemblyInput.SelectedCharacter.ToDebugString());
		
		OnComplete.ExecuteIfBound(FMetaHumanAssemblyOutput());
		return;
	}

	FMetaHumanAssemblyOutput AssemblyOutput;
	FMetaHumanOutfitPipelineAssemblyOutput& OutfitAssemblyOutput = AssemblyOutput.PipelineAssemblyOutput.InitializeAs<FMetaHumanOutfitPipelineAssemblyOutput>();
	OutfitAssemblyOutput.Outfit = SelectedCharacterOutfit->Outfit;
	OutfitAssemblyOutput.OutfitMesh = SelectedCharacterOutfit->OutfitMesh;
	OutfitAssemblyOutput.HeadHiddenFaceMap = SelectedCharacterOutfit->HeadHiddenFaceMap;
	OutfitAssemblyOutput.BodyHiddenFaceMap = SelectedCharacterOutfit->BodyHiddenFaceMap;

	const USkinnedAsset* MaterialSource = OutfitAssemblyOutput.Outfit ? static_cast<USkinnedAsset*>(OutfitAssemblyOutput.Outfit) : static_cast<USkinnedAsset*>(OutfitAssemblyOutput.OutfitMesh);
	if (MaterialSource)
	{
		FMetaHumanPostAssemblyParameterOutput PostAssemblyParameterOutput;

		const TArray<FSkeletalMaterial>& MaterialSections = MaterialSource->GetMaterials();
		const FString ItemFriendlyName = Params.BaseItemPath.GetPathEntry(Params.BaseItemPath.GetNumPathEntries() - 1).ToAssetNameString();
		const UE::MetaHuman::MaterialUtils::FFetchSlotNameDelegate FetchSlotName = UE::MetaHuman::MaterialUtils::MakeFetchSlotNameDelegate(MaterialSections);

		UE::MetaHuman::MaterialUtils::ProcessAssemblyParameters(
			OverrideMaterials,
			RuntimeMaterialParameters,
			ItemFriendlyName,
			TEXT("Clothing"),
			MaterialSections.Num(),
			FetchSlotName,
			UE::MetaHuman::MaterialUtils::MakeFetchSlotMaterialDelegate(MaterialSections),
			Params.OuterForGeneratedObjects,
			OutfitAssemblyOutput.OverrideMaterials,
			PostAssemblyParameterOutput.Parameters,
			AssemblyOutput.Metadata);

		if (PostAssemblyParameterOutput.Parameters.IsValid())
		{
			if (const FInstancedPropertyBag* AssemblyParameters = Params.AssemblyParameters.Find(Params.BaseItemPath))
			{
				UE::MetaHuman::MaterialUtils::SetInstanceParameters(
					RuntimeMaterialParameters,
					UE::MetaHuman::MaterialUtils::CastMaterialMapToDynamic(OutfitAssemblyOutput.OverrideMaterials),
					FetchSlotName,
					*AssemblyParameters);

				PostAssemblyParameterOutput.Parameters.CopyMatchingValuesByName(*AssemblyParameters);
			}

			AssemblyOutput.PostAssemblyParameters.Edit().Add(Params.BaseItemPath, MoveTemp(PostAssemblyParameterOutput));
		}
	}
			
	OnComplete.ExecuteIfBound(MoveTemp(AssemblyOutput));
}

void UMetaHumanOutfitPipeline::SetPostAssemblyParameters(const FSetPostAssemblyParametersParams& Params, FInstancedStruct& InOutItemAssemblyOutput) const
{
	check(Params.ModifiedPostAssemblyParameters.IsValid());

	FMetaHumanOutfitPipelineAssemblyOutput* OutfitAssemblyOutput = InOutItemAssemblyOutput.GetMutablePtr<FMetaHumanOutfitPipelineAssemblyOutput>();
	if (!OutfitAssemblyOutput)
	{
		// Incompatible or incomplete assembly output
		return;
	}

	const USkinnedAsset* MaterialSource = OutfitAssemblyOutput->Outfit ? static_cast<USkinnedAsset*>(OutfitAssemblyOutput->Outfit) : static_cast<USkinnedAsset*>(OutfitAssemblyOutput->OutfitMesh);
	if (!MaterialSource)
	{
		return;
	}

	UE::MetaHuman::MaterialUtils::SetInstanceParameters(
		RuntimeMaterialParameters,
		UE::MetaHuman::MaterialUtils::CastMaterialMapToDynamic(OutfitAssemblyOutput->OverrideMaterials),
		UE::MetaHuman::MaterialUtils::MakeFetchSlotNameDelegate(MaterialSource->GetMaterials()),
		Params.ModifiedPostAssemblyParameters);
}

TNotNull<const UMetaHumanCharacterPipelineSpecification*> UMetaHumanOutfitPipeline::GetSpecification() const
{
	return Specification;
}

void UMetaHumanOutfitPipeline::ApplyOutfitAssemblyOutputToClothComponent(const FMetaHumanOutfitPipelineAssemblyOutput& InOutfitAssemblyOutput, UChaosClothComponent* InClothComponent)
{
	InClothComponent->SetAsset(InOutfitAssemblyOutput.Outfit);
	InClothComponent->EmptyOverrideMaterials();

	const TArray<FName> SlotNames = InClothComponent->GetMaterialSlotNames();

	for (const TPair<FName, TObjectPtr<UMaterialInterface>>& OverrideMaterial : InOutfitAssemblyOutput.OverrideMaterials)
	{
		for (int32 MaterialIndex = 0; MaterialIndex < SlotNames.Num(); ++MaterialIndex)
		{
			if (OverrideMaterial.Key == SlotNames[MaterialIndex])
			{
				InClothComponent->SetMaterial(MaterialIndex, OverrideMaterial.Value);
			}
		}
	}
}

void UMetaHumanOutfitPipeline::ApplyOutfitAssemblyOutputToMeshComponent(const FMetaHumanOutfitPipelineAssemblyOutput& InOutfitAssemblyOutput, USkeletalMeshComponent* InMeshComponent, bool bUpdateSkelMesh)
{
	InMeshComponent->SetSkeletalMesh(InOutfitAssemblyOutput.OutfitMesh);
	InMeshComponent->EmptyOverrideMaterials();

	for (const TPair<FName, TObjectPtr<UMaterialInterface>>& OverrideMaterial : InOutfitAssemblyOutput.OverrideMaterials)
	{
		const int32 MaterialIndex = InMeshComponent->GetMaterialIndex(OverrideMaterial.Key);
		if (MaterialIndex != INDEX_NONE)
		{
			InMeshComponent->SetMaterial(MaterialIndex, OverrideMaterial.Value);
		}
	}

	if (bUpdateSkelMesh && InOutfitAssemblyOutput.OutfitMesh)
	{
		TArray<FSkeletalMaterial> Materials = InOutfitAssemblyOutput.OutfitMesh->GetMaterials();

		for (int32 i = 0; i < Materials.Num(); ++i)
		{
			if (const TObjectPtr<UMaterialInterface>* FoundMaterial = InOutfitAssemblyOutput.OverrideMaterials.Find(Materials[i].MaterialSlotName))
			{
				Materials[i].MaterialInterface = *FoundMaterial;
			}
		}

		InOutfitAssemblyOutput.OutfitMesh->SetMaterials(Materials);
	}
}
