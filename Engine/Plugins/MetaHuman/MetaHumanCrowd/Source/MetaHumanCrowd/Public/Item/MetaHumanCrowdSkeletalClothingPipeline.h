// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanItemPipeline.h"
#include "MetaHumanPaletteItemKey.h"

#include "Item/MetaHumanCrowdOutfitPipeline.h"

#include "MetaHumanCrowdSkeletalClothingPipeline.generated.h"

class UMetaHumanWardrobeItem;
class USkeletalMesh;

/**
 * Crowd item pipeline for skeletal-mesh clothing.
 */
UCLASS(Blueprintable, EditInlineNew)
class UMetaHumanCrowdSkeletalClothingPipeline : public UMetaHumanItemPipeline
{
	GENERATED_BODY()

public:
	UMetaHumanCrowdSkeletalClothingPipeline();

#if WITH_EDITOR
	virtual void SetDefaultEditorPipeline() override;

	virtual const UMetaHumanItemEditorPipeline* GetEditorPipeline() const override;
#endif

	virtual void AssembleItem(const FAssembleItemParams& Params, const FOnAssemblyComplete& OnComplete) const override;

	virtual void SetPostAssemblyParameters(const FSetPostAssemblyParametersParams& Params, FInstancedStruct& InOutItemAssemblyOutput) const override;

	virtual TNotNull<const UMetaHumanCharacterPipelineSpecification*> GetSpecification() const override;

	/**
	 * The source wardrobe item, whose pipeline must be a UMetaHumanSkeletalMeshPipeline.
	 * Override materials and runtime material parameters are read from this source pipeline.
	 */
	UPROPERTY(EditAnywhere, Category = "Pipeline")
	TObjectPtr<const UMetaHumanWardrobeItem> SourceSkeletalClothingItem;

	// Key is material slot name
	UPROPERTY(EditAnywhere, Category = "Pipeline")
	TMap<FName, FMetaHumanCrowdOutfitInstancedMaterial> InstancedComponentOverrideMaterials;

private:
#if WITH_EDITOR
	TSubclassOf<UMetaHumanItemEditorPipeline> GetEditorPipelineClass() const;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, NoClear, Instanced, Category = "Pipeline", meta = (FullyExpand, AllowedClasses = "/Script/MetaHumanCrowdEditor.MetaHumanCrowdSkeletalClothingEditorPipeline"))
	TObjectPtr<UMetaHumanItemEditorPipeline> EditorPipeline;
#endif

	UPROPERTY(Transient)
	TObjectPtr<UMetaHumanCharacterPipelineSpecification> Specification;
};
