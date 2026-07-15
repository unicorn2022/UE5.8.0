// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataflowRendering/DataflowRenderableTypeSettings.h"
#include "DataflowRendering/DataflowRenderableTypeUtils.h"

#include "DataflowSphereRenderableType.generated.h"

UCLASS(MinimalAPI)
class UDataflowSphereRenderSettings : public UDataflowRenderableTypeSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Surface", meta = (ShowOnlyInnerProperties))
	FDataflowSimpleColorCommonRenderSettings ColorSettings;
};

UCLASS(MinimalAPI, AutoExpandCategories = "Surface|ConstantColor, Surface|RandomColor, Surface|ColorBySize")
class UDataflowSphereArrayRenderSettings : public UDataflowRenderableTypeSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Surface", meta = (ShowOnlyInnerProperties))
	FDataflowColorCommonRenderSettings ColorSettings;
};

namespace UE::Dataflow::Private
{
	void RegisterSphereRenderableTypes();
}
