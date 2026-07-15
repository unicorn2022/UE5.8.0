// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanDefaultEditorPipelineBase.h"

#include "MetaHumanDefaultEditorPipelineLegacy.generated.h"

/**
 * Editor pipeline for UMetaHumanDefaultPipelineLegacy
 */
UCLASS(EditInlineNew)
class METAHUMANDEFAULTEDITORPIPELINE_API UMetaHumanDefaultEditorPipelineLegacy : public UMetaHumanDefaultEditorPipelineBase
{
	GENERATED_BODY()

public:
	// Blueprint actor class to duplicate when creating a new blueprint
	UPROPERTY(EditAnywhere, Category = "Character")
	TSubclassOf<AActor> TemplateClass;

	virtual UBlueprint* WriteActorBlueprint(const FWriteBlueprintSettings& InWriteBlueprintSettings) const override;
	virtual bool UpdateActorBlueprint(const UMetaHumanInstance* InCharacterInstance, UBlueprint* InBlueprint) const override;
	
protected:
	/**
	 * Optimize bone counts for all skeletal mesh components except for the Body in the given Blueprint,
	 * by removing unskinned bones from their skeletal meshes for all LODs.
	 *
	 * Intended to be called in late assembly after all skeletal meshes have been assigned to their components.
	 *
	 * @param InBlueprint The Blueprint being modified.
	 * @param ActorCDO The generated class default object used to gather subobjects.
	 */
	static void OptimizeBoneCounts(UBlueprint* InBlueprint, AActor* ActorCDO);
};
