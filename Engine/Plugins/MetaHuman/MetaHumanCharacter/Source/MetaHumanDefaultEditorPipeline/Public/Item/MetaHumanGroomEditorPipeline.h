// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanItemEditorPipeline.h"

#include "MetaHumanGroomEditorPipeline.generated.h"

class USkeletalMesh;

USTRUCT()
struct FMetaHumanGroomPipelineBuildInput
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<TObjectPtr<USkeletalMesh>> BindingMeshes;

	UPROPERTY()
	TArray<int32> FaceLODs;
};

UCLASS(EditInlineNew)
class METAHUMANDEFAULTEDITORPIPELINE_API UMetaHumanGroomEditorPipeline : public UMetaHumanItemEditorPipeline
{
	GENERATED_BODY()

public:
	UMetaHumanGroomEditorPipeline();

	virtual UE::Tasks::TTask<FMetaHumanPaletteBuiltData> BuildItem(const FBuildItemParams& Params) const override;

	virtual TNotNull<const UMetaHumanCharacterEditorPipelineSpecification*> GetSpecification() const override;

private:
	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterEditorPipelineSpecification> Specification;
};
