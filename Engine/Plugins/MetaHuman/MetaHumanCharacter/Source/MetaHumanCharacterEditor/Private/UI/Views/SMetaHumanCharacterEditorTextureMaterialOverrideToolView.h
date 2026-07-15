// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/NotifyHook.h"
#include "SMetaHumanCharacterEditorToolView.h"

class UMetaHumanCharacterEditorTextureMaterialOverrideTool;

/** View for displaying the Texture & Material Overrides Tool in the MetaHuman Character editor */
class SMetaHumanCharacterEditorTextureMaterialOverrideToolView : public SMetaHumanCharacterEditorToolView, public FNotifyHook
{
public:
	SLATE_BEGIN_ARGS(SMetaHumanCharacterEditorTextureMaterialOverrideToolView)
		{}
	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs, UMetaHumanCharacterEditorTextureMaterialOverrideTool* InTool);

protected:
	//~ Begin SMetaHumanCharacterEditorToolView interface
	virtual UInteractiveToolPropertySet* GetToolProperties() const override;
	virtual void MakeToolView() override;
	//~ End of SMetaHumanCharacterEditorToolView interface

	//~ Begin FNotifyHook interface
	virtual void NotifyPreChange(FProperty* PropertyAboutToChange) override;
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;
	//~ End of FNotifyHook interface
};
