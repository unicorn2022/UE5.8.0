// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataflowRendering/DataflowRenderableTypeSettings.h"
#include "DataflowRendering/DataflowRenderableTypeUtils.h"

#include "DataflowPhysicsAssetRenderableType.generated.h"

UCLASS(MinimalAPI)
class UDataflowPhysicsAssetRenderSettings : public UDataflowRenderableTypeSettings
{
	GENERATED_BODY()

	UDataflowPhysicsAssetRenderSettings(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

public:
	UPROPERTY(EditAnywhere, Category = "Surface", meta = (ShowOnlyInnerProperties))
	FDataflowSimpleColorCommonRenderSettings ColorSettings;
};

namespace UE::Dataflow::Private
{
	void RegisterPhysicsAssetRenderableTypes();
}
