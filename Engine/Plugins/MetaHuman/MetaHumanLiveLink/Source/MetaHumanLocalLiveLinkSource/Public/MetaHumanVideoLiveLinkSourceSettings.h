// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanLocalLiveLinkSourceSettings.h"

#include "Nodes/HyprsenseRealtimeNode.h"

#include "MetaHumanVideoLiveLinkSourceSettings.generated.h"



UCLASS()
class METAHUMANLOCALLIVELINKSOURCE_API UMetaHumanVideoLiveLinkSourceSettings : public UMetaHumanLocalLiveLinkSourceSettings
{
public:

	GENERATED_BODY()

	/* The models to be used by the realtime pipeline */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Create", meta = (DisplayName = "Models"))
	FMonocularAnimationPipelineModels MonocularAnimationPipelineModels;
};