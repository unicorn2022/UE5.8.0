// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdMode.h"

#include "UAFChooserEditorMode.generated.h"

class FStateTreeBindingExtension;
class FStateTreeBindingsChildrenCustomization;
class IDetailsView;
class IMessageLogListing;

UCLASS(MinimalAPI, Transient)
class UUAFChooserEditorMode : public UEdMode
{
	GENERATED_BODY()
public:
	const static FEditorModeID EM_UAFChooser;
	
	UUAFChooserEditorMode();

	virtual void Enter() override;
	virtual void Exit() override;
	virtual void CreateToolkit() override;
	virtual void BindCommands() override;

};
