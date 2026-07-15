// Copyright Epic Games, Inc. All Rights Reserved.

#include "Item/MetaHumanCrowdSkeletalClothingEditorPipeline.h"

#include "Item/MetaHumanCrowdOutfitEditorPipeline.h"
#include "Item/MetaHumanCrowdSkeletalClothingPipeline.h"
#include "Item/MetaHumanSkeletalMeshPipeline.h"
#include "Item/MetaHumanSkeletalMeshEditorPipeline.h"
#include "MetaHumanCrowdEditorLog.h"
#include "MetaHumanCrowdEditorUtilities.h"
#include "MetaHumanCrowdTypes.h"
#include "MetaHumanWardrobeItem.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Logging/StructuredLog.h"

UMetaHumanCrowdSkeletalClothingEditorPipeline::UMetaHumanCrowdSkeletalClothingEditorPipeline()
{
	Specification = CreateDefaultSubobject<UMetaHumanCharacterEditorPipelineSpecification>("Specification");
	Specification->BuildInputStruct = FMetaHumanCrowdOutfitBuildInput::StaticStruct();
}

UE::Tasks::TTask<FMetaHumanPaletteBuiltData> UMetaHumanCrowdSkeletalClothingEditorPipeline::BuildItem(const FBuildItemParams& Params) const
{
	const UMetaHumanCrowdSkeletalClothingPipeline* RuntimePipeline = Cast<UMetaHumanCrowdSkeletalClothingPipeline>(GetRuntimePipeline());
	if (!RuntimePipeline)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "Runtime pipeline for item {ItemPath} must be a UMetaHumanCrowdSkeletalClothingPipeline", Params.ItemPath.ToDebugString());

		return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
	}

	if (!Params.BuildInput.GetPtr<FMetaHumanCrowdOutfitBuildInput>())
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "Build input not provided to Crowd Skeletal Clothing pipeline during build");

		return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
	}

	UObject* LoadedAsset = Params.WardrobeItem->PrincipalAsset.LoadSynchronous();
	USkeletalMesh* SourceMesh = Cast<USkeletalMesh>(LoadedAsset);
	if (!SourceMesh)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "Crowd Skeletal Clothing pipeline failed to load skeletal mesh {SkeletalMesh} during build", Params.WardrobeItem->PrincipalAsset.ToString());

		return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
	}

	const FMetaHumanCrowdOutfitBuildInput& ClothingBuildInput = Params.BuildInput.Get<FMetaHumanCrowdOutfitBuildInput>();

	FMetaHumanPaletteBuiltData BuiltDataResult;
	FMetaHumanPipelineBuiltData& ClothingBuiltData = BuiltDataResult.ItemBuiltData.Edit().Add(Params.ItemPath);
	ClothingBuiltData.DefaultUnpackSubfolder = FString::Format(TEXT("SkeletalClothing/{0}"), { LoadedAsset->GetName() });

	FMetaHumanCrowdOutfitBuildOutput& ClothingBuildOutput = ClothingBuiltData.BuildOutput.InitializeAs<FMetaHumanCrowdOutfitBuildOutput>();

	// Fetch the body hidden face map from the source skeletal-mesh editor pipeline
	if (RuntimePipeline->SourceSkeletalClothingItem)
	{
		const UMetaHumanSkeletalMeshEditorPipeline* SourceEditorPipeline = Cast<UMetaHumanSkeletalMeshEditorPipeline>(
			RuntimePipeline->SourceSkeletalClothingItem->GetEditorPipeline());
		if (SourceEditorPipeline
			&& SourceEditorPipeline->BodyHiddenFaceMapTexture.Texture
			&& SourceEditorPipeline->BodyHiddenFaceMapTexture.Texture->Source.IsValid())
		{
			ClothingBuildOutput.BodyHiddenFaceMap = SourceEditorPipeline->BodyHiddenFaceMapTexture;
		}
	}

	// Copy the geometry for each body, to match the output of the outfit pipeline
	FMetaHumanCrowdMeshGeometryBundle SharedBundle;
	UE::MetaHuman::CrowdEditorUtilities::ExtractGeometryBundle(SourceMesh, SharedBundle);

	for (const TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdOutfitFitTarget>& Pair : ClothingBuildInput.FitTargets)
	{
		if (CompatibleBodies.Num() > 0 && !CompatibleBodies.Contains(Pair.Value.BodyCharacter))
		{
			// This body isn't supported by this clothing item
			continue;
		}

		ClothingBuildOutput.BodyToOutfitGeometryMap.Add(Pair.Key, SharedBundle);
	}

	// Generate Assembly Parameters
	if (RuntimePipeline->SourceSkeletalClothingItem)
	{
		if (const UMetaHumanSkeletalMeshPipeline* SourcePipeline = Cast<UMetaHumanSkeletalMeshPipeline>(RuntimePipeline->SourceSkeletalClothingItem->GetPipeline()))
		{
			const TArray<FSkeletalMaterial>& MaterialSections = SourceMesh->GetMaterials();

			UE::MetaHuman::MaterialUtils::GenerateAssemblyParameters(
				SourcePipeline->OverrideMaterials,
				SourcePipeline->RuntimeMaterialParameters,
				MaterialSections.Num(),
				UE::MetaHuman::MaterialUtils::MakeFetchSlotNameDelegate(MaterialSections),
				UE::MetaHuman::MaterialUtils::MakeFetchSlotMaterialDelegate(MaterialSections),
				ClothingBuiltData.AssemblyParameters);
		}
	}

	return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>(MoveTemp(BuiltDataResult));
}

TNotNull<const UMetaHumanCharacterEditorPipelineSpecification*> UMetaHumanCrowdSkeletalClothingEditorPipeline::GetSpecification() const
{
	return Specification;
}
