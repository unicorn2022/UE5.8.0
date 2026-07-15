// Copyright Epic Games, Inc. All Rights Reserved. 

#include "AssetDefinition_InterchangeEditorBlueprintPipeline.h"
#include "InterchangePipelineFactories.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_InterchangeEditorBlueprintPipeline"

UFactory* UAssetDefinition_InterchangeEditorBlueprintPipeline::GetFactoryForBlueprintType(UBlueprint* InBlueprint) const
{
	UInterchangeEditorBlueprintPipelineBaseFactory* InterchangeEditorBlueprintPipelineBaseFactory = NewObject<UInterchangeEditorBlueprintPipelineBaseFactory>();
	InterchangeEditorBlueprintPipelineBaseFactory->ParentClass = TSubclassOf<UInterchangeEditorPipelineBase>(*InBlueprint->GeneratedClass);
	return InterchangeEditorBlueprintPipelineBaseFactory;
}

#undef LOCTEXT_NAMESPACE
