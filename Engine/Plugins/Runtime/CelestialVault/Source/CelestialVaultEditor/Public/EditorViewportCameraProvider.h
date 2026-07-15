// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewportCameraProvider.h"

class FEditorViewportCameraProvider : public IViewportCameraProvider
{
public:
	virtual bool GetCamera(FVector& OutLocation, FRotator& OutRotation) const override;
};
