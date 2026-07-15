// Copyright Epic Games, Inc. All Rights Reserved.

#include "Item/MetaHumanSkeletalMeshEditorPipeline.h"

#include "Item/MetaHumanSkeletalMeshPipeline.h"
#include "Item/MetaHumanMaterialPipelineCommon.h"
#include "MetaHumanDefaultEditorPipelineLog.h"
#include "MetaHumanWardrobeItem.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "SkeletalMeshTypes.h"
#include "Logging/StructuredLog.h"

UMetaHumanSkeletalMeshEditorPipeline::UMetaHumanSkeletalMeshEditorPipeline()
{
	Specification = CreateDefaultSubobject<UMetaHumanCharacterEditorPipelineSpecification>("Specification");
}

UE::Tasks::TTask<FMetaHumanPaletteBuiltData> UMetaHumanSkeletalMeshEditorPipeline::BuildItem(const FBuildItemParams& Params) const
{
	const UMetaHumanSkeletalMeshPipeline* RuntimePipeline = Cast<UMetaHumanSkeletalMeshPipeline>(GetRuntimePipeline());
	if (!RuntimePipeline)
	{
		UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Error, "Runtime pipeline for item {ItemPath} must be a UMetaHumanSkeletalMeshPipeline", Params.ItemPath.ToDebugString());
		
		return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
	}

	UObject* LoadedAsset = Params.WardrobeItem->PrincipalAsset.LoadSynchronous();
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(LoadedAsset);
	if (!SkeletalMesh)
	{
		UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Error, "SkeletalMesh pipeline failed to load skeletal mesh {SkeletalMesh} during build", Params.WardrobeItem->PrincipalAsset.ToString());
		
		return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
	}

	FMetaHumanPaletteBuiltData BuiltDataResult;
	FMetaHumanPipelineBuiltData& SkeletalMeshBuiltData = BuiltDataResult.ItemBuiltData.Edit().Add(Params.ItemPath);
	FMetaHumanSkeletalMeshPipelineBuildOutput& SkelMeshBuildOutput = SkeletalMeshBuiltData.BuildOutput.InitializeAs<FMetaHumanSkeletalMeshPipelineBuildOutput>();

	SkelMeshBuildOutput.Mesh = SkeletalMesh;

	if (HeadHiddenFaceMapTexture.Texture)
	{
		if (HeadHiddenFaceMapTexture.Texture->Source.IsValid())
		{
			SkelMeshBuildOutput.HeadHiddenFaceMap = HeadHiddenFaceMapTexture;
		}
		else
		{
			UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Warning, "Head hidden face map {Texture} on skeletal mesh pipeline {Pipeline} has no source data and can't be used",
				HeadHiddenFaceMapTexture.Texture->GetPathName(), GetPathName());
		}
	}
						
	if (BodyHiddenFaceMapTexture.Texture)
	{
		if (BodyHiddenFaceMapTexture.Texture->Source.IsValid())
		{
			SkelMeshBuildOutput.BodyHiddenFaceMap = BodyHiddenFaceMapTexture;
		}
		else
		{
			UE_LOGFMT(LogMetaHumanDefaultEditorPipeline, Warning, "Body hidden face map {Texture} on skeletal mesh pipeline {Pipeline} has no source data and can't be used",
				BodyHiddenFaceMapTexture.Texture->GetPathName(), GetPathName());
		}
	}

	// Generate Assembly Parameters
	if (SkeletalMesh)
	{
		const TArray<FSkeletalMaterial>& MaterialSections = SkeletalMesh->GetMaterials();

		UE::MetaHuman::MaterialUtils::GenerateAssemblyParameters(
			RuntimePipeline->OverrideMaterials,
			RuntimePipeline->RuntimeMaterialParameters,
			MaterialSections.Num(),
			UE::MetaHuman::MaterialUtils::MakeFetchSlotNameDelegate(MaterialSections),
			UE::MetaHuman::MaterialUtils::MakeFetchSlotMaterialDelegate(MaterialSections),
			SkeletalMeshBuiltData.AssemblyParameters);
	}

	return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>(MoveTemp(BuiltDataResult));
}

TNotNull<const UMetaHumanCharacterEditorPipelineSpecification*> UMetaHumanSkeletalMeshEditorPipeline::GetSpecification() const
{
	return Specification;
}
