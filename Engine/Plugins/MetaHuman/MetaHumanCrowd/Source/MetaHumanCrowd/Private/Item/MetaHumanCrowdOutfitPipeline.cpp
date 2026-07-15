// Copyright Epic Games, Inc. All Rights Reserved.

#include "Item/MetaHumanCrowdOutfitPipeline.h"

#include "Components/InstancedSkinnedMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Item/MetaHumanOutfitPipeline.h"
#include "MetaHumanCrowdMaterialUtils.h"
#include "MetaHumanItemEditorPipeline.h"
#include "MetaHumanCrowdLog.h"
#include "MetaHumanWardrobeItem.h"

#include "Logging/StructuredLog.h"

UMetaHumanCrowdOutfitPipeline::UMetaHumanCrowdOutfitPipeline()
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
void UMetaHumanCrowdOutfitPipeline::SetDefaultEditorPipeline()
{
	EditorPipeline = nullptr;

	const TSubclassOf<UMetaHumanItemEditorPipeline> EditorPipelineClass = GetEditorPipelineClass();
	if (EditorPipelineClass)
	{
		EditorPipeline = NewObject<UMetaHumanItemEditorPipeline>(this, EditorPipelineClass);
	}
}

const UMetaHumanItemEditorPipeline* UMetaHumanCrowdOutfitPipeline::GetEditorPipeline() const
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

TSubclassOf<UMetaHumanItemEditorPipeline> UMetaHumanCrowdOutfitPipeline::GetEditorPipelineClass() const
{
	const TSoftClassPtr<UMetaHumanItemEditorPipeline> SoftEditorPipelineClass(FSoftObjectPath(TEXT("/Script/MetaHumanCrowdEditor.MetaHumanCrowdOutfitEditorPipeline")));
	
	return SoftEditorPipelineClass.Get();
}
#endif

void UMetaHumanCrowdOutfitPipeline::AssembleItem(const FAssembleItemParams& Params, const FOnAssemblyComplete& OnComplete) const
{
	const FInstancedStruct& BuildOutput = Params.ItemBuiltData[Params.BaseItemPath].BuildOutput;

	if (!BuildOutput.GetPtr<FMetaHumanCrowdOutfitBuildOutput>())
	{
		UE_LOGFMT(LogMetaHumanCrowd, Error, "Build output not provided to Crowd Outfit pipeline during assembly");
		
		OnComplete.ExecuteIfBound(FMetaHumanAssemblyOutput());
		return;
	}

	if (!Params.AssemblyInput.GetPtr<FMetaHumanCrowdOutfitAssemblyInput>())
	{
		UE_LOGFMT(LogMetaHumanCrowd, Error, "Assembly input not provided to Crowd Outfit pipeline during assembly");
		
		OnComplete.ExecuteIfBound(FMetaHumanAssemblyOutput());
		return;
	}

	const FMetaHumanCrowdOutfitBuildOutput& OutfitBuildOutput = BuildOutput.Get<FMetaHumanCrowdOutfitBuildOutput>();
	const FMetaHumanCrowdOutfitAssemblyInput& OutfitAssemblyInput = Params.AssemblyInput.Get<FMetaHumanCrowdOutfitAssemblyInput>();

	FMetaHumanAssemblyOutput AssemblyOutput;
	FMetaHumanCrowdOutfitAssemblyOutput& OutfitAssemblyOutput = AssemblyOutput.PipelineAssemblyOutput.InitializeAs<FMetaHumanCrowdOutfitAssemblyOutput>();

	const FMetaHumanCrowdMeshGeometryBundle* OutfitBundle = OutfitBuildOutput.BodyToOutfitGeometryMap.Find(OutfitAssemblyInput.BodyItem);
	if (!OutfitBundle)
	{
		UE_LOGFMT(LogMetaHumanCrowd, Error, "Failed to find outfit geometry for Body item {Body} during Crowd Outfit assembly", OutfitAssemblyInput.BodyItem.ToDebugString());
		
		OnComplete.ExecuteIfBound(FMetaHumanAssemblyOutput());
		return;
	}

	if (SourceOutfitItem)
	{
		if (const UMetaHumanOutfitPipeline* SourcePipeline = Cast<UMetaHumanOutfitPipeline>(SourceOutfitItem->GetPipeline()))
		{
			FMetaHumanPostAssemblyParameterOutput PostAssemblyParameterOutput;

			const TArray<FSkeletalMaterial>& MaterialSections = OutfitBundle->Materials;
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
				OutfitAssemblyOutput.MeshComponentOverrideMaterials,
				PostAssemblyParameterOutput.Parameters,
				AssemblyOutput.Metadata);

			if (PostAssemblyParameterOutput.Parameters.IsValid())
			{
				if (const FInstancedPropertyBag* AssemblyParameters = Params.AssemblyParameters.Find(Params.BaseItemPath))
				{
					UE::MetaHuman::MaterialUtils::SetInstanceParameters(
						FilteredRuntimeMaterialParameters,
						UE::MetaHuman::MaterialUtils::CastMaterialMapToDynamic(OutfitAssemblyOutput.MeshComponentOverrideMaterials),
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
		OutfitAssemblyOutput.InstancedMaterialData.Add(Pair.Key, Pair.Value.InstancedComponentMaterial);
	}

	// Size the ISKM-wide custom-data buffer once and validate that authored offsets across slots
	// don't collide. The validation is a soft warning -- runtime writes still happen via absolute
	// offsets and silent corruption follows if an overlap goes uncaught here.
	//
	// Validation is scoped to the slot names present on this fitted mesh: fitted outfits derived
	// from a single source may carry disjoint subsets of the source's slots, and offsets that
	// would collide on the source's full slot set can be safely shared between slots that never
	// co-occur on any one fitted mesh.
	{
		// Fitted outfits are produced per (source outfit, body) pair, so include both in the
		// validation context: the same source can produce different fitted variants per body
		// with disjoint slot subsets, and we want a warning to point at the specific variant.
		const FString SourceOutfitName = Params.BaseItemPath.GetPathEntry(Params.BaseItemPath.GetNumPathEntries() - 1).ToAssetNameString();
		const FString ItemContext = FString::Printf(TEXT("%s fitted to body %s"), *SourceOutfitName, *OutfitAssemblyInput.BodyItem.ToDebugString());

		TArray<FName> MeshSlotNames;
		MeshSlotNames.Reserve(OutfitBundle->Materials.Num());
		for (const FSkeletalMaterial& Material : OutfitBundle->Materials)
		{
			MeshSlotNames.Add(Material.MaterialSlotName);
		}

		UE::MetaHuman::MaterialUtils::ValidateNoOverlappingCustomDataOffsets(InstancedComponentOverrideMaterials, MeshSlotNames, ItemContext);
		OutfitAssemblyOutput.InstancedMeshCustomDataFloats.SetNumZeroed(
			UE::MetaHuman::MaterialUtils::ComputeISKMCustomDataSize(InstancedComponentOverrideMaterials));
	}

	// Populate initial values from assembly parameters into the ISKM-wide buffer
	if (SourceOutfitItem && !OutfitAssemblyOutput.InstancedMeshCustomDataFloats.IsEmpty())
	{
		if (const UMetaHumanOutfitPipeline* SourcePipeline = Cast<UMetaHumanOutfitPipeline>(SourceOutfitItem->GetPipeline()))
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
				OutfitAssemblyOutput.InstancedMeshCustomDataFloats);

			if (const FInstancedPropertyBag* AssemblyParameters = Params.AssemblyParameters.Find(Params.BaseItemPath))
			{
				UE::MetaHuman::MaterialUtils::SetInstanceParametersOnCustomData(
					FilteredRuntimeMaterialParameters,
					InstancedComponentOverrideMaterials,
					*AssemblyParameters,
					OutfitAssemblyOutput.InstancedMeshCustomDataFloats);
			}
		}
	}

	// For slots where the source pipeline specified an override material, but which have no
	// InstancedComponentOverrideMaterials entry, use the mesh component material for the 
	// instanced meshes as well.
	//
	// This means that if an outfit's materials don't support per-instance custom data, we
	// default to using the regular version of the material that is controlled with material
	// parameters.
	for (const TPair<FName, TObjectPtr<UMaterialInterface>>& Override : OutfitAssemblyOutput.MeshComponentOverrideMaterials)
	{
		if (!InstancedComponentOverrideMaterials.Contains(Override.Key))
		{
			OutfitAssemblyOutput.InstancedMaterialData.FindOrAdd(Override.Key) = Override.Value;
		}
	}

	OnComplete.ExecuteIfBound(MoveTemp(AssemblyOutput));
}

void UMetaHumanCrowdOutfitPipeline::SetPostAssemblyParameters(const FSetPostAssemblyParametersParams& Params, FInstancedStruct& InOutItemAssemblyOutput) const
{
	check(Params.ModifiedPostAssemblyParameters.IsValid());

	FMetaHumanCrowdOutfitAssemblyOutput* OutfitAssemblyOutput = InOutItemAssemblyOutput.GetMutablePtr<FMetaHumanCrowdOutfitAssemblyOutput>();
	if (!OutfitAssemblyOutput)
	{
		// Incompatible or incomplete assembly output
		return;
	}
		
	if (SourceOutfitItem)
	{
		if (const UMetaHumanOutfitPipeline* SourcePipeline = Cast<UMetaHumanOutfitPipeline>(SourceOutfitItem->GetPipeline()))
		{
			const TArray<FMetaHumanMaterialParameter> FilteredRuntimeMaterialParameters = 
				UE::MetaHuman::MaterialUtils::FilterToCrowdSupportedParameters(SourcePipeline->RuntimeMaterialParameters);

			UE::MetaHuman::MaterialUtils::SetInstanceParameters(
				FilteredRuntimeMaterialParameters,
				UE::MetaHuman::MaterialUtils::CastMaterialMapToDynamic(OutfitAssemblyOutput->MeshComponentOverrideMaterials),
				UE::MetaHuman::MaterialUtils::FFetchSlotNameDelegate(),
				Params.ModifiedPostAssemblyParameters);

			// Update instanced mesh custom data floats
			if (!OutfitAssemblyOutput->InstancedMeshCustomDataFloats.IsEmpty())
			{
				UE::MetaHuman::MaterialUtils::SetInstanceParametersOnCustomData(
					FilteredRuntimeMaterialParameters,
					InstancedComponentOverrideMaterials,
					Params.ModifiedPostAssemblyParameters,
					OutfitAssemblyOutput->InstancedMeshCustomDataFloats);
			}
		}
	}
}

TNotNull<const UMetaHumanCharacterPipelineSpecification*> UMetaHumanCrowdOutfitPipeline::GetSpecification() const
{
	return Specification;
}
