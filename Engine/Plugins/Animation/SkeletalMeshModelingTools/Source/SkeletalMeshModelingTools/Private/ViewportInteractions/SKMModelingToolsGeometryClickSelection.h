// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewportInteractions/ViewportClickInteraction.h"
#include "SKMModelingToolsGeometryClickSelection.generated.h"

class UGeometrySelectionManager;

UCLASS(MinimalAPI, Transient)
class USkeletalMeshModelingToolsGeometryClickSelection
	: public UViewportClickInteraction
{
	GENERATED_BODY()

public:
	USkeletalMeshModelingToolsGeometryClickSelection();
	virtual void BuildBehaviors() override;
	virtual void OnClickUp(const FInputDeviceRay& InClickPos) override;

	void BindSelectionManager(UGeometrySelectionManager* InSelectionManager);

	virtual TArray<FEditorModeID> GetUnsupportedModes() const override {return {};}
	
protected:
	
	TWeakObjectPtr<UGeometrySelectionManager> SelectionManager;
};

