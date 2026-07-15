// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tools/MetaHumanCharacterExternalImportTool.h"
#include "SMetaHumanCharacterEditorImportToolView.h"

/**
 * Host wrapper widget for externally-contributed import tools.
 * Derives from the abstract SMetaHumanCharacterEditorImportToolView so it participates
 * in the standard import tool view hierarchy (scroll-offset save/restore, toolkit guards)
 * without inheriting FNotifyHook or any internal import tool assumptions.
 *
 * Created by FMetaHumanCharacterEditorModeToolkit::CreateToolView() when it detects a
 * tool contributed via IMetaHumanImportToolFeature.
 */
class SMetaHumanCharacterEditorExternalImportToolView : public SMetaHumanCharacterEditorImportToolView
{
public:
	SLATE_BEGIN_ARGS(SMetaHumanCharacterEditorExternalImportToolView) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<SWidget> InContent, UMetaHumanCharacterExternalImportTool* InExtTool);

protected:
	//~ Begin SMetaHumanCharacterEditorToolView interface
	virtual UInteractiveToolPropertySet* GetToolProperties() const override;
	virtual void MakeToolView() override;
	//~ End of SMetaHumanCharacterEditorToolView interface

	//~ Begin SMetaHumanCharacterEditorImportToolView interface
	virtual bool IsImportButtonEnabled() const override;
	virtual FReply OnImportButtonClicked() override;
	virtual FText GetWarningText() const override;
	// GetImportButtonText() not overridden — base default "Apply" is correct
	//~ End of SMetaHumanCharacterEditorImportToolView interface

private:
	TSharedPtr<SWidget> ExternalContent;
	TWeakObjectPtr<UMetaHumanCharacterExternalImportTool> ExtTool;
};
