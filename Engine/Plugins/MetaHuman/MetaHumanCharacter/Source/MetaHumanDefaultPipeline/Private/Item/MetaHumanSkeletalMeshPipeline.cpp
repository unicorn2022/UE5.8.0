// Copyright Epic Games, Inc. All Rights Reserved.

#include "Item/MetaHumanSkeletalMeshPipeline.h"

#include "MetaHumanDefaultPipelineLog.h"
#include "MetaHumanItemEditorPipeline.h"

#include "Algo/Find.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Logging/StructuredLog.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"

UMetaHumanSkeletalMeshPipeline::UMetaHumanSkeletalMeshPipeline()
{
	// Initialize the specification
	{
		Specification = CreateDefaultSubobject<UMetaHumanCharacterPipelineSpecification>("Specification", /*bTransient*/ true);

		Specification->BuildOutputStruct = FMetaHumanSkeletalMeshPipelineBuildOutput::StaticStruct();
		Specification->AssemblyOutputStruct = FMetaHumanSkeletalMeshPipelineAssemblyOutput::StaticStruct();
	}
}

#if WITH_EDITOR
void UMetaHumanSkeletalMeshPipeline::SetDefaultEditorPipeline()
{
	EditorPipeline = nullptr;

	const TSubclassOf<UMetaHumanItemEditorPipeline> EditorPipelineClass = GetEditorPipelineClass();
	if (EditorPipelineClass)
	{
		EditorPipeline = NewObject<UMetaHumanItemEditorPipeline>(this, EditorPipelineClass);
	}
}

const UMetaHumanItemEditorPipeline* UMetaHumanSkeletalMeshPipeline::GetEditorPipeline() const
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

TSubclassOf<UMetaHumanItemEditorPipeline> UMetaHumanSkeletalMeshPipeline::GetEditorPipelineClass() const
{
	const TSoftClassPtr<UMetaHumanItemEditorPipeline> SoftEditorPipelineClass(FSoftObjectPath(TEXT("/Script/MetaHumanDefaultEditorPipeline.MetaHumanSkeletalMeshEditorPipeline")));
	
	return SoftEditorPipelineClass.Get();
}
#endif

void UMetaHumanSkeletalMeshPipeline::AssembleItem(const FAssembleItemParams& Params, const FOnAssemblyComplete& OnComplete) const
{
	const FInstancedStruct& BuildOutput = Params.ItemBuiltData[Params.BaseItemPath].BuildOutput;
	const FMetaHumanSkeletalMeshPipelineBuildOutput* SkelMeshBuildOutputPtr = BuildOutput.GetPtr<FMetaHumanSkeletalMeshPipelineBuildOutput>();
	if (!SkelMeshBuildOutputPtr)
	{
		UE_LOGFMT(LogMetaHumanDefaultPipeline, Error, "Invalid type for build output provided to SkeletalMesh pipeline during assembly");

		OnComplete.ExecuteIfBound(FMetaHumanAssemblyOutput());
		return;
	}

	const FMetaHumanSkeletalMeshPipelineBuildOutput& SkelMeshBuildOutput = *SkelMeshBuildOutputPtr;
	if (!SkelMeshBuildOutput.Mesh)
	{
		UE_LOGFMT(LogMetaHumanDefaultPipeline, Error, "The requested skeletal mesh is missing");
		OnComplete.ExecuteIfBound(FMetaHumanAssemblyOutput());
		return;
	}

	FMetaHumanAssemblyOutput AssemblyOutput;
	FMetaHumanSkeletalMeshPipelineAssemblyOutput& SkeletalMeshAssemblyOutput = AssemblyOutput.PipelineAssemblyOutput.InitializeAs<FMetaHumanSkeletalMeshPipelineAssemblyOutput>();
	SkeletalMeshAssemblyOutput.SkelMesh = SkelMeshBuildOutput.Mesh;
	SkeletalMeshAssemblyOutput.AnimBlueprintToUse = AnimBlueprintToUse;
	SkeletalMeshAssemblyOutput.HeadHiddenFaceMap = SkelMeshBuildOutput.HeadHiddenFaceMap;
	SkeletalMeshAssemblyOutput.BodyHiddenFaceMap = SkelMeshBuildOutput.BodyHiddenFaceMap;

	FMetaHumanPostAssemblyParameterOutput PostAssemblyParameterOutput;
	{
		const TArray<FSkeletalMaterial>& MaterialSections = SkelMeshBuildOutput.Mesh->GetMaterials();
		const FString ItemFriendlyName = Params.BaseItemPath.GetPathEntry(Params.BaseItemPath.GetNumPathEntries() - 1).ToAssetNameString();
		const UE::MetaHuman::MaterialUtils::FFetchSlotNameDelegate FetchSlotName = UE::MetaHuman::MaterialUtils::MakeFetchSlotNameDelegate(MaterialSections);

		UE::MetaHuman::MaterialUtils::ProcessAssemblyParameters(
			OverrideMaterials,
			RuntimeMaterialParameters,
			ItemFriendlyName,
			TEXT("SkelMesh"),
			MaterialSections.Num(),
			FetchSlotName,
			UE::MetaHuman::MaterialUtils::MakeFetchSlotMaterialDelegate(MaterialSections),
			Params.OuterForGeneratedObjects,
			SkeletalMeshAssemblyOutput.OverrideMaterials,
			PostAssemblyParameterOutput.Parameters,
			AssemblyOutput.Metadata);

		if (PostAssemblyParameterOutput.Parameters.IsValid())
		{
			if (const FInstancedPropertyBag* AssemblyParameters = Params.AssemblyParameters.Find(Params.BaseItemPath))
			{
				UE::MetaHuman::MaterialUtils::SetInstanceParameters(
					RuntimeMaterialParameters,
					UE::MetaHuman::MaterialUtils::CastMaterialMapToDynamic(SkeletalMeshAssemblyOutput.OverrideMaterials),
					FetchSlotName,
					*AssemblyParameters);

				PostAssemblyParameterOutput.Parameters.CopyMatchingValuesByName(*AssemblyParameters);
			}

			AssemblyOutput.PostAssemblyParameters.Edit().Add(Params.BaseItemPath, MoveTemp(PostAssemblyParameterOutput));
		}
	}

	OnComplete.ExecuteIfBound(MoveTemp(AssemblyOutput));
}

void UMetaHumanSkeletalMeshPipeline::SetPostAssemblyParameters(const FSetPostAssemblyParametersParams& Params, FInstancedStruct& InOutItemAssemblyOutput) const
{
	check(Params.ModifiedPostAssemblyParameters.IsValid());

	FMetaHumanSkeletalMeshPipelineAssemblyOutput* SkelMeshAssemblyOutput = InOutItemAssemblyOutput.GetMutablePtr<FMetaHumanSkeletalMeshPipelineAssemblyOutput>();
	if (!SkelMeshAssemblyOutput)
	{
		// Incompatible or incomplete assembly output
		return;
	}

	const USkinnedAsset* MaterialSource = SkelMeshAssemblyOutput->SkelMesh;
	if (!MaterialSource)
	{
		return;
	}

	UE::MetaHuman::MaterialUtils::SetInstanceParameters(
		RuntimeMaterialParameters,
		UE::MetaHuman::MaterialUtils::CastMaterialMapToDynamic(SkelMeshAssemblyOutput->OverrideMaterials),
		UE::MetaHuman::MaterialUtils::MakeFetchSlotNameDelegate(MaterialSource->GetMaterials()),
		Params.ModifiedPostAssemblyParameters);
}

TNotNull<const UMetaHumanCharacterPipelineSpecification*> UMetaHumanSkeletalMeshPipeline::GetSpecification() const
{
	return Specification;
}

void UMetaHumanSkeletalMeshPipeline::ApplySkeletalMeshAssemblyOutputToSkeletalMeshComponent(
	const FMetaHumanSkeletalMeshPipelineAssemblyOutput& InAssemblyOutput,
	USkeletalMeshComponent* InComponent,
	USkeletalMeshComponent* InLeaderComponent)
{
	InComponent->SetSkeletalMesh(InAssemblyOutput.SkelMesh);

	// If there is an AnimBP specified by the pipeline, use that
	if (UAnimBlueprint* AnimBlueprint = InAssemblyOutput.AnimBlueprintToUse.LoadSynchronous())
	{
		InComponent->SetLeaderPoseComponent(nullptr);
		InComponent->SetAnimInstanceClass(AnimBlueprint->GetClass());
	}
	// If there is post process AnimBP on the skeletal mesh, use that
	else if (InAssemblyOutput.SkelMesh && InAssemblyOutput.SkelMesh->GetPostProcessAnimBlueprint() != nullptr)
	{
		InComponent->SetLeaderPoseComponent(nullptr);
		InComponent->SetAnimInstanceClass(nullptr);
	}
	// If no AnimBP is defined, use the leader pose component
	else if (InLeaderComponent)
	{
		InComponent->SetLeaderPoseComponent(InLeaderComponent);
		InComponent->SetAnimInstanceClass(nullptr);
	}

	InComponent->EmptyOverrideMaterials();

	for (const TPair<FName, TObjectPtr<UMaterialInterface>>& OverrideMaterial : InAssemblyOutput.OverrideMaterials)
	{
		const int32 MaterialIndex = InComponent->GetMaterialIndex(OverrideMaterial.Key);
		if (MaterialIndex != INDEX_NONE)
		{
			InComponent->SetMaterial(MaterialIndex, OverrideMaterial.Value);
		}
	}
}
