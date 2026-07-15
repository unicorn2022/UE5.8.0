// Copyright Epic Games, Inc. All Rights Reserved.

#include "Item/MetaHumanCrowdSkeletalClothingPipeline.h"

#include "Components/InstancedSkinnedMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Item/MetaHumanSkeletalMeshPipeline.h"
#include "MetaHumanCrowdMaterialUtils.h"
#include "MetaHumanItemEditorPipeline.h"
#include "MetaHumanCrowdLog.h"
#include "MetaHumanWardrobeItem.h"

#include "Logging/StructuredLog.h"

UMetaHumanCrowdSkeletalClothingPipeline::UMetaHumanCrowdSkeletalClothingPipeline()
{
	// Initialize the specification
	{
		Specification = CreateDefaultSubobject<UMetaHumanCharacterPipelineSpecification>("Specification", /*bTransient*/ true);

		Specification->BuildOutputStruct = FMetaHumanCrowdOutfitBuildOutput::StaticStruct();
		Specification->AssemblyInputStruct = FMetaHumanCrowdOutfitAssemblyInput::StaticStruct();
		Specification->AssemblyOutputStruct = FMetaHumanCrowdOutfitAssemblyOutput::StaticStruct();
	}
}

#if WITH_EDITOR
void UMetaHumanCrowdSkeletalClothingPipeline::SetDefaultEditorPipeline()
{
	EditorPipeline = nullptr;

	const TSubclassOf<UMetaHumanItemEditorPipeline> EditorPipelineClass = GetEditorPipelineClass();
	if (EditorPipelineClass)
	{
		EditorPipeline = NewObject<UMetaHumanItemEditorPipeline>(this, EditorPipelineClass);
	}
}

const UMetaHumanItemEditorPipeline* UMetaHumanCrowdSkeletalClothingPipeline::GetEditorPipeline() const
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

TSubclassOf<UMetaHumanItemEditorPipeline> UMetaHumanCrowdSkeletalClothingPipeline::GetEditorPipelineClass() const
{
	const TSoftClassPtr<UMetaHumanItemEditorPipeline> SoftEditorPipelineClass(FSoftObjectPath(TEXT("/Script/MetaHumanCrowdEditor.MetaHumanCrowdSkeletalClothingEditorPipeline")));

	return SoftEditorPipelineClass.Get();
}
#endif

void UMetaHumanCrowdSkeletalClothingPipeline::AssembleItem(const FAssembleItemParams& Params, const FOnAssemblyComplete& OnComplete) const
{
	const FInstancedStruct& BuildOutput = Params.ItemBuiltData[Params.BaseItemPath].BuildOutput;

	if (!BuildOutput.GetPtr<FMetaHumanCrowdOutfitBuildOutput>())
	{
		UE_LOGFMT(LogMetaHumanCrowd, Error, "Build output not provided to Crowd Skeletal Clothing pipeline during assembly");

		OnComplete.ExecuteIfBound(FMetaHumanAssemblyOutput());
		return;
	}

	if (!Params.AssemblyInput.GetPtr<FMetaHumanCrowdOutfitAssemblyInput>())
	{
		UE_LOGFMT(LogMetaHumanCrowd, Error, "Assembly input not provided to Crowd Skeletal Clothing pipeline during assembly");

		OnComplete.ExecuteIfBound(FMetaHumanAssemblyOutput());
		return;
	}

	const FMetaHumanCrowdOutfitBuildOutput& ClothingBuildOutput = BuildOutput.Get<FMetaHumanCrowdOutfitBuildOutput>();
	const FMetaHumanCrowdOutfitAssemblyInput& ClothingAssemblyInput = Params.AssemblyInput.Get<FMetaHumanCrowdOutfitAssemblyInput>();

	FMetaHumanAssemblyOutput AssemblyOutput;
	FMetaHumanCrowdOutfitAssemblyOutput& ClothingAssemblyOutput = AssemblyOutput.PipelineAssemblyOutput.InitializeAs<FMetaHumanCrowdOutfitAssemblyOutput>();

	const FMetaHumanCrowdMeshGeometryBundle* ClothingBundle = ClothingBuildOutput.BodyToOutfitGeometryMap.Find(ClothingAssemblyInput.BodyItem);
	if (!ClothingBundle)
	{
		UE_LOGFMT(LogMetaHumanCrowd, Error, "Failed to find skeletal clothing geometry for Body item {Body} during Crowd Skeletal Clothing assembly", ClothingAssemblyInput.BodyItem.ToDebugString());

		OnComplete.ExecuteIfBound(FMetaHumanAssemblyOutput());
		return;
	}

	if (SourceSkeletalClothingItem)
	{
		if (const UMetaHumanSkeletalMeshPipeline* SourcePipeline = Cast<UMetaHumanSkeletalMeshPipeline>(SourceSkeletalClothingItem->GetPipeline()))
		{
			FMetaHumanPostAssemblyParameterOutput PostAssemblyParameterOutput;

			const TArray<FSkeletalMaterial>& MaterialSections = ClothingBundle->Materials;
			const FString ItemFriendlyName = Params.BaseItemPath.GetPathEntry(Params.BaseItemPath.GetNumPathEntries() - 1).ToAssetNameString();
			const UE::MetaHuman::MaterialUtils::FFetchSlotNameDelegate FetchSlotName = UE::MetaHuman::MaterialUtils::MakeFetchSlotNameDelegate(MaterialSections);

			// Crowd pipelines rebuild material slots while generating meshes, so SlotIndices
			// targeting is unsupported. Drop any such parameters now (with a warning) and only use
			// the filtered set.
			const TArray<FMetaHumanMaterialParameter> FilteredRuntimeMaterialParameters =
				UE::MetaHuman::MaterialUtils::FilterToCrowdSupportedParameters(SourcePipeline->RuntimeMaterialParameters, /*bLogWarningOnFilter*/ true, ItemFriendlyName);

			UE::MetaHuman::MaterialUtils::ProcessAssemblyParameters(
				SourcePipeline->OverrideMaterials,
				FilteredRuntimeMaterialParameters,
				ItemFriendlyName,
				TEXT("Clothing"),
				MaterialSections.Num(),
				FetchSlotName,
				UE::MetaHuman::MaterialUtils::MakeFetchSlotMaterialDelegate(MaterialSections),
				Params.OuterForGeneratedObjects,
				ClothingAssemblyOutput.MeshComponentOverrideMaterials,
				PostAssemblyParameterOutput.Parameters,
				AssemblyOutput.Metadata);

			if (PostAssemblyParameterOutput.Parameters.IsValid())
			{
				if (const FInstancedPropertyBag* AssemblyParameters = Params.AssemblyParameters.Find(Params.BaseItemPath))
				{
					UE::MetaHuman::MaterialUtils::SetInstanceParameters(
						FilteredRuntimeMaterialParameters,
						UE::MetaHuman::MaterialUtils::CastMaterialMapToDynamic(ClothingAssemblyOutput.MeshComponentOverrideMaterials),
						FetchSlotName,
						*AssemblyParameters);

					PostAssemblyParameterOutput.Parameters.CopyMatchingValuesByName(*AssemblyParameters);
				}

				AssemblyOutput.PostAssemblyParameters.Edit().Add(Params.BaseItemPath, MoveTemp(PostAssemblyParameterOutput));
			}
		}
	}

	// Populate instanced mesh material bindings from the pipeline's InstancedComponentOverrideMaterials
	for (const TPair<FName, FMetaHumanCrowdOutfitInstancedMaterial>& Pair : InstancedComponentOverrideMaterials)
	{
		ClothingAssemblyOutput.InstancedMaterialData.Add(Pair.Key, Pair.Value.InstancedComponentMaterial);
	}

	// Size the ISKM-wide custom-data buffer once and validate that authored offsets across slots
	// don't collide. The validation is a soft warning -- runtime writes still happen via absolute
	// offsets and silent corruption follows if an overlap goes uncaught here.
	{
		const FString ItemFriendlyName = Params.BaseItemPath.GetPathEntry(Params.BaseItemPath.GetNumPathEntries() - 1).ToAssetNameString();
		const FString ItemContext = FString::Printf(TEXT("%s on body %s"), *ItemFriendlyName, *ClothingAssemblyInput.BodyItem.ToDebugString());

		TArray<FName> MeshSlotNames;
		MeshSlotNames.Reserve(ClothingBundle->Materials.Num());
		for (const FSkeletalMaterial& Material : ClothingBundle->Materials)
		{
			MeshSlotNames.Add(Material.MaterialSlotName);
		}

		UE::MetaHuman::MaterialUtils::ValidateNoOverlappingCustomDataOffsets(InstancedComponentOverrideMaterials, MeshSlotNames, ItemContext);
		ClothingAssemblyOutput.InstancedMeshCustomDataFloats.SetNumZeroed(
			UE::MetaHuman::MaterialUtils::ComputeISKMCustomDataSize(InstancedComponentOverrideMaterials));
	}

	// Populate initial values from assembly parameters into the ISKM-wide buffer
	if (SourceSkeletalClothingItem && !ClothingAssemblyOutput.InstancedMeshCustomDataFloats.IsEmpty())
	{
		if (const UMetaHumanSkeletalMeshPipeline* SourcePipeline = Cast<UMetaHumanSkeletalMeshPipeline>(SourceSkeletalClothingItem->GetPipeline()))
		{
			// Silent filter -- SlotIndices-targeted parameters already warned above (or
			// will warn once the branch above runs). Skipping them here is sufficient.
			TArray<FMetaHumanMaterialParameter> FilteredRuntimeMaterialParameters;
			FilteredRuntimeMaterialParameters.Reserve(SourcePipeline->RuntimeMaterialParameters.Num());
			for (const FMetaHumanMaterialParameter& Parameter : SourcePipeline->RuntimeMaterialParameters)
			{
				if (Parameter.SlotTarget != EMetaHumanRuntimeMaterialParameterSlotTarget::SlotIndices)
				{
					FilteredRuntimeMaterialParameters.Add(Parameter);
				}
			}

			UE::MetaHuman::MaterialUtils::SetInstanceParameterDefaultsOnCustomData(
				FilteredRuntimeMaterialParameters,
				InstancedComponentOverrideMaterials,
				ClothingAssemblyOutput.InstancedMeshCustomDataFloats);

			if (const FInstancedPropertyBag* AssemblyParameters = Params.AssemblyParameters.Find(Params.BaseItemPath))
			{
				UE::MetaHuman::MaterialUtils::SetInstanceParametersOnCustomData(
					FilteredRuntimeMaterialParameters,
					InstancedComponentOverrideMaterials,
					*AssemblyParameters,
					ClothingAssemblyOutput.InstancedMeshCustomDataFloats);
			}
		}
	}

	// For slots where the source pipeline specified an override material, but which have no
	// InstancedComponentOverrideMaterials entry, use the mesh component material for the
	// instanced meshes as well.
	//
	// This means that if a clothing item's materials don't support per-instance custom data, we
	// default to using the regular version of the material that is controlled with material
	// parameters.
	for (const TPair<FName, TObjectPtr<UMaterialInterface>>& Override : ClothingAssemblyOutput.MeshComponentOverrideMaterials)
	{
		if (!InstancedComponentOverrideMaterials.Contains(Override.Key))
		{
			ClothingAssemblyOutput.InstancedMaterialData.FindOrAdd(Override.Key) = Override.Value;
		}
	}

	OnComplete.ExecuteIfBound(MoveTemp(AssemblyOutput));
}

void UMetaHumanCrowdSkeletalClothingPipeline::SetPostAssemblyParameters(const FSetPostAssemblyParametersParams& Params, FInstancedStruct& InOutItemAssemblyOutput) const
{
	check(Params.ModifiedPostAssemblyParameters.IsValid());

	FMetaHumanCrowdOutfitAssemblyOutput* ClothingAssemblyOutput = InOutItemAssemblyOutput.GetMutablePtr<FMetaHumanCrowdOutfitAssemblyOutput>();
	if (!ClothingAssemblyOutput)
	{
		// Incompatible or incomplete assembly output
		return;
	}

	if (SourceSkeletalClothingItem)
	{
		if (const UMetaHumanSkeletalMeshPipeline* SourcePipeline = Cast<UMetaHumanSkeletalMeshPipeline>(SourceSkeletalClothingItem->GetPipeline()))
		{
			const TArray<FMetaHumanMaterialParameter> FilteredRuntimeMaterialParameters =
				UE::MetaHuman::MaterialUtils::FilterToCrowdSupportedParameters(SourcePipeline->RuntimeMaterialParameters);

			UE::MetaHuman::MaterialUtils::SetInstanceParameters(
				FilteredRuntimeMaterialParameters,
				UE::MetaHuman::MaterialUtils::CastMaterialMapToDynamic(ClothingAssemblyOutput->MeshComponentOverrideMaterials),
				UE::MetaHuman::MaterialUtils::FFetchSlotNameDelegate(),
				Params.ModifiedPostAssemblyParameters);

			// Update instanced mesh custom data floats
			if (!ClothingAssemblyOutput->InstancedMeshCustomDataFloats.IsEmpty())
			{
				UE::MetaHuman::MaterialUtils::SetInstanceParametersOnCustomData(
					FilteredRuntimeMaterialParameters,
					InstancedComponentOverrideMaterials,
					Params.ModifiedPostAssemblyParameters,
					ClothingAssemblyOutput->InstancedMeshCustomDataFloats);
			}
		}
	}
}

TNotNull<const UMetaHumanCharacterPipelineSpecification*> UMetaHumanCrowdSkeletalClothingPipeline::GetSpecification() const
{
	return Specification;
}
