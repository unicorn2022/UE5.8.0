// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryMaskReadComponent.h"

#include "Engine/CanvasRenderTarget2D.h"
#include "GeometryMaskCanvas.h"

void UGeometryMaskReadComponent::SetParameters(FGeometryMaskReadParameters& InParameters)
{
	Parameters = InParameters;
	TryResolveCanvas();
}

bool UGeometryMaskReadComponent::ForEachUsedCanvasName(TFunctionRef<bool(FName)> InFunc) const
{
	return InFunc(Parameters.CanvasName);
}

bool UGeometryMaskReadComponent::TryResolveCanvas()
{
	return TryResolveNamedCanvas(Parameters.CanvasName);
}
