// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanDefaultEditorPipelineBase.h"

#include "MetaHumanDefaultEditorPipeline.generated.h"

/**
 * Editor pipeline for UMetaHumanDefaultPipeline
 */
UCLASS(EditInlineNew)
class METAHUMANDEFAULTEDITORPIPELINE_API UMetaHumanDefaultEditorPipeline : public UMetaHumanDefaultEditorPipelineBase
{
	GENERATED_BODY()

public:
	virtual UBlueprint* WriteActorBlueprint(const FWriteBlueprintSettings& InWriteBlueprintSettings) const override;
	virtual bool UpdateActorBlueprint(const UMetaHumanInstance* InCharacterInstance, UBlueprint* InBlueprint) const override;
};
