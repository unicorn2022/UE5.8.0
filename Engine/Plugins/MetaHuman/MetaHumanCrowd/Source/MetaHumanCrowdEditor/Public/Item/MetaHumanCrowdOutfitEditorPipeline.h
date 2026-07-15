// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanItemEditorPipeline.h"

#include "MetaHumanCrowdOutfitEditorPipeline.generated.h"

class UDataflow;
class UMetaHumanCharacter;
class USkeletalMesh;
class USkeleton;

USTRUCT()
struct FMetaHumanCrowdOutfitFitTarget
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TSoftObjectPtr<UMetaHumanCharacter> BodyCharacter;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> MergedHeadAndBodyMesh;
};

USTRUCT()
struct FMetaHumanCrowdOutfitBuildInput
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TMap<FMetaHumanPaletteItemKey, FMetaHumanCrowdOutfitFitTarget> FitTargets;

	UPROPERTY()
	TObjectPtr<UDataflow> OutfitResizeDataflowAsset;
};

UCLASS(EditInlineNew)
class UMetaHumanCrowdOutfitEditorPipeline : public UMetaHumanItemEditorPipeline
{
	GENERATED_BODY()

public:
	UMetaHumanCrowdOutfitEditorPipeline();

	virtual UE::Tasks::TTask<FMetaHumanPaletteBuiltData> BuildItem(const FBuildItemParams& Params) const override;

	virtual TNotNull<const UMetaHumanCharacterEditorPipelineSpecification*> GetSpecification() const override;
	
	UPROPERTY(EditAnywhere, Category = "Outfit")
	TArray<TSoftObjectPtr<UMetaHumanCharacter>> CompatibleBodies;
	
private:
	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterEditorPipelineSpecification> Specification;
};
