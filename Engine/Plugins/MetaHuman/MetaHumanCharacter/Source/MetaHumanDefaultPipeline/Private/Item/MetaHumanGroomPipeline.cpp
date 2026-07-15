// Copyright Epic Games, Inc. All Rights Reserved.

#include "Item/MetaHumanGroomPipeline.h"

#include "MetaHumanDefaultPipelineLog.h"
#include "MetaHumanItemEditorPipeline.h"

#include "Algo/Find.h"
#include "GroomAsset.h"
#include "GroomBindingAsset.h"
#include "GroomComponent.h"
#include "Logging/StructuredLog.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"

UMetaHumanGroomPipeline::UMetaHumanGroomPipeline()
{
	// Initialize the specification
	{
		Specification = CreateDefaultSubobject<UMetaHumanCharacterPipelineSpecification>("Specification", /*bTransient*/ true);

		Specification->BuildOutputStruct = FMetaHumanGroomPipelineBuildOutput::StaticStruct();
		Specification->AssemblyInputStruct = FMetaHumanGroomPipelineAssemblyInput::StaticStruct();
		Specification->AssemblyOutputStruct = FMetaHumanGroomPipelineAssemblyOutput::StaticStruct();
	}
}

#if WITH_EDITOR
void UMetaHumanGroomPipeline::SetDefaultEditorPipeline()
{
	EditorPipeline = nullptr;

	const TSubclassOf<UMetaHumanItemEditorPipeline> EditorPipelineClass = GetEditorPipelineClass();
	if (EditorPipelineClass)
	{
		EditorPipeline = NewObject<UMetaHumanItemEditorPipeline>(this, EditorPipelineClass);
	}
}

const UMetaHumanItemEditorPipeline* UMetaHumanGroomPipeline::GetEditorPipeline() const
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

TSubclassOf<UMetaHumanItemEditorPipeline> UMetaHumanGroomPipeline::GetEditorPipelineClass() const
{
	const TSoftClassPtr<UMetaHumanItemEditorPipeline> SoftEditorPipelineClass(FSoftObjectPath(TEXT("/Script/MetaHumanDefaultEditorPipeline.MetaHumanGroomEditorPipeline")));
	
	return SoftEditorPipelineClass.Get();
}
#endif

void UMetaHumanGroomPipeline::AssembleItem(const FAssembleItemParams& Params, const FOnAssemblyComplete& OnComplete) const
{
	const FInstancedStruct& BuildOutput = Params.ItemBuiltData[Params.BaseItemPath].BuildOutput;

	if (!BuildOutput.GetPtr<FMetaHumanGroomPipelineBuildOutput>())
	{
		UE_LOGFMT(LogMetaHumanDefaultPipeline, Error, "Build output not provided to Groom pipeline during assembly");
		
		OnComplete.ExecuteIfBound(FMetaHumanAssemblyOutput());
		return;
	}

	if (!Params.AssemblyInput.GetPtr<FMetaHumanGroomPipelineAssemblyInput>())
	{
		UE_LOGFMT(LogMetaHumanDefaultPipeline, Error, "Assembly input not provided to Groom pipeline during assembly");
		
		OnComplete.ExecuteIfBound(FMetaHumanAssemblyOutput());
		return;
	}

	const FMetaHumanGroomPipelineBuildOutput& GroomBuildOutput = BuildOutput.Get<FMetaHumanGroomPipelineBuildOutput>();
	const FMetaHumanGroomPipelineAssemblyInput& GroomAssemblyInput = Params.AssemblyInput.Get<FMetaHumanGroomPipelineAssemblyInput>();

	FMetaHumanAssemblyOutput AssemblyOutput;
	FMetaHumanGroomPipelineAssemblyOutput& GroomAssemblyOutput = AssemblyOutput.PipelineAssemblyOutput.InitializeAs<FMetaHumanGroomPipelineAssemblyOutput>();

	if (!GroomBuildOutput.bRequiresBinding)
	{
		// This groom is already into the skin material and doesn't need a binding
		OnComplete.ExecuteIfBound(MoveTemp(AssemblyOutput));
		return;
	}

	const TObjectPtr<UGroomBindingAsset>* GroomBindingPtr = GroomBuildOutput.Bindings.FindByPredicate(
	[TargetMesh = GroomAssemblyInput.TargetMesh](const TObjectPtr<UGroomBindingAsset>& Binding)
	{
		return Binding
			&& TargetMesh == Binding->GetTargetSkeletalMesh();
	});

	if (!GroomBindingPtr)
	{
		UE_LOGFMT(LogMetaHumanDefaultPipeline, Error, "The requested skeletal mesh ({Mesh}) was not found in the Groom pipeline's build output", GetPathNameSafe(GroomAssemblyInput.TargetMesh));
		OnComplete.ExecuteIfBound(FMetaHumanAssemblyOutput());
		return;
	}

	GroomAssemblyOutput.Binding = *GroomBindingPtr;

	FMetaHumanPostAssemblyParameterOutput PostAssemblyParameterOutput;

	if (GroomAssemblyOutput.Binding)
	{
		if (const UGroomAsset* Groom = GroomAssemblyOutput.Binding->GetGroom())
		{
			const TArray<FHairGroupsMaterial>& HairGroupMaterials = Groom->GetHairGroupsMaterials();
			const FString ItemFriendlyName = Params.BaseItemPath.GetPathEntry(Params.BaseItemPath.GetNumPathEntries() - 1).ToAssetNameString();
			const UE::MetaHuman::MaterialUtils::FFetchSlotNameDelegate FetchSlotName = MakeFetchSlotNameDelegate(HairGroupMaterials);

			UE::MetaHuman::MaterialUtils::ProcessAssemblyParameters(
				OverrideMaterials,
				RuntimeMaterialParameters,
				ItemFriendlyName,
				TEXT("Grooms"),
				HairGroupMaterials.Num(),
				FetchSlotName,
				MakeFetchSlotMaterialDelegate(HairGroupMaterials),
				Params.OuterForGeneratedObjects,
				GroomAssemblyOutput.OverrideMaterials,
				PostAssemblyParameterOutput.Parameters,
				AssemblyOutput.Metadata);

			TMap<FName, TObjectPtr<UMaterialInstanceDynamic>> DynamicMaterialMap = UE::MetaHuman::MaterialUtils::CastMaterialMapToDynamic(GroomAssemblyOutput.OverrideMaterials);

			for (TPair<FName, TObjectPtr<UMaterialInstanceDynamic>>& Pair : DynamicMaterialMap)
			{
				// Set some common parameters on any material instances that were created
				OverrideInitialMaterialValues(Pair.Value, Pair.Key);
			}

			if (PostAssemblyParameterOutput.Parameters.IsValid())
			{
				if (const FInstancedPropertyBag* AssemblyParameters = Params.AssemblyParameters.Find(Params.BaseItemPath))
				{
					UE::MetaHuman::MaterialUtils::SetInstanceParameters(
						RuntimeMaterialParameters,
						DynamicMaterialMap,
						FetchSlotName,
						*AssemblyParameters);

					PostAssemblyParameterOutput.Parameters.CopyMatchingValuesByName(*AssemblyParameters);
				}

				AssemblyOutput.PostAssemblyParameters.Edit().Add(Params.BaseItemPath, MoveTemp(PostAssemblyParameterOutput));
			}
		}

		// Populate face mesh skin MIDs so SetPostAssemblyParameters can update baked groom color params
		for (const FSkeletalMaterial& SkelMat : GroomAssemblyInput.TargetMesh->GetMaterials())
		{
			if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(SkelMat.MaterialInterface))
			{
				GroomAssemblyOutput.FaceMeshOverrideMaterials.Add(SkelMat.MaterialSlotName, MID);
			}
		}
	}

	OnComplete.ExecuteIfBound(MoveTemp(AssemblyOutput));
}

void UMetaHumanGroomPipeline::SetPostAssemblyParameters(const FSetPostAssemblyParametersParams& Params, FInstancedStruct& InOutItemAssemblyOutput) const
{
	check(Params.ModifiedPostAssemblyParameters.IsValid());

	FMetaHumanGroomPipelineAssemblyOutput* GroomAssemblyOutput = InOutItemAssemblyOutput.GetMutablePtr<FMetaHumanGroomPipelineAssemblyOutput>();
	if (!GroomAssemblyOutput
		|| !GroomAssemblyOutput->Binding)
	{
		// Incompatible or incomplete assembly output
		return;
	}

	const UGroomAsset* Groom = GroomAssemblyOutput->Binding->GetGroom();
	if (!Groom)
	{
		return;
	}

	UE::MetaHuman::MaterialUtils::SetInstanceParameters(
		RuntimeMaterialParameters,
		UE::MetaHuman::MaterialUtils::CastMaterialMapToDynamic(GroomAssemblyOutput->OverrideMaterials),
		MakeFetchSlotNameDelegate(Groom->GetHairGroupsMaterials()),
		Params.ModifiedPostAssemblyParameters);
}

TNotNull<const UMetaHumanCharacterPipelineSpecification*> UMetaHumanGroomPipeline::GetSpecification() const
{
	return Specification;
}

void UMetaHumanGroomPipeline::ApplyGroomAssemblyOutputToGroomComponent(const FMetaHumanGroomPipelineAssemblyOutput& GroomAssemblyOutput, UGroomComponent* GroomComponent)
{
	GroomComponent->SetGroomAsset(GroomAssemblyOutput.Binding ? GroomAssemblyOutput.Binding->GetGroom() : nullptr, GroomAssemblyOutput.Binding);

	GroomComponent->EmptyOverrideMaterials();
	for (const TPair<FName, TObjectPtr<UMaterialInterface>>& OverrideMaterial : GroomAssemblyOutput.OverrideMaterials)
	{
		const int32 MaterialIndex = GroomComponent->GetMaterialIndex(OverrideMaterial.Key);
		if (MaterialIndex != INDEX_NONE)
		{
			GroomComponent->SetMaterial(MaterialIndex, OverrideMaterial.Value);
		}
	}
}

UE::MetaHuman::MaterialUtils::FFetchSlotNameDelegate UMetaHumanGroomPipeline::MakeFetchSlotNameDelegate(TConstArrayView<FHairGroupsMaterial> HairMaterials)
{
	return UE::MetaHuman::MaterialUtils::FFetchSlotNameDelegate::CreateLambda([HairMaterials](int32 Index) -> FName
		{
			return HairMaterials.IsValidIndex(Index) ? HairMaterials[Index].SlotName : NAME_None;
		});
}

UE::MetaHuman::MaterialUtils::FFetchSlotMaterialDelegate UMetaHumanGroomPipeline::MakeFetchSlotMaterialDelegate(TConstArrayView<FHairGroupsMaterial> HairMaterials)
{
	return UE::MetaHuman::MaterialUtils::FFetchSlotMaterialDelegate::CreateLambda([HairMaterials](int32 Index)
		{
			return HairMaterials.IsValidIndex(Index) ? HairMaterials[Index].Material : nullptr;
		});
}
