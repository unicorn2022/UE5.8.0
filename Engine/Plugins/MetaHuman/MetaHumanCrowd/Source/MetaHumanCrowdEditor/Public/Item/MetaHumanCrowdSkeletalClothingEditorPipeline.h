// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanItemEditorPipeline.h"

#include "MetaHumanCrowdSkeletalClothingEditorPipeline.generated.h"

class UMetaHumanCharacter;
class USkeleton;

/**
 * Editor pipeline for the crowd Skeletal Mesh clothing item pipeline.
 */
UCLASS(EditInlineNew)
class UMetaHumanCrowdSkeletalClothingEditorPipeline : public UMetaHumanItemEditorPipeline
{
	GENERATED_BODY()

public:
	UMetaHumanCrowdSkeletalClothingEditorPipeline();

	virtual UE::Tasks::TTask<FMetaHumanPaletteBuiltData> BuildItem(const FBuildItemParams& Params) const override;

	virtual TNotNull<const UMetaHumanCharacterEditorPipelineSpecification*> GetSpecification() const override;

	UPROPERTY(EditAnywhere, Category = "SkeletalClothing")
	TArray<TSoftObjectPtr<UMetaHumanCharacter>> CompatibleBodies;

private:
	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterEditorPipelineSpecification> Specification;
};
