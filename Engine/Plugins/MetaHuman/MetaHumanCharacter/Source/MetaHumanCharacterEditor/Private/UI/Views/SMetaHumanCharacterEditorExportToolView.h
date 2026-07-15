// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/NotifyHook.h"
#include "SMetaHumanCharacterEditorToolView.h"

class FReply;
class IDetailsView;
class UMetaHumanCharacterEditorExportToolBase;

/** Generic view for all Export tools in the MetaHuman Character editor */
class SMetaHumanCharacterEditorExportToolView
	: public SMetaHumanCharacterEditorToolView
	, public FNotifyHook
{
public:
	SLATE_BEGIN_ARGS(SMetaHumanCharacterEditorExportToolView) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMetaHumanCharacterEditorExportToolBase* InTool);

protected:

	//~Begin SMetaHumanCharacterEditorToolView interface
	virtual UInteractiveToolPropertySet* GetToolProperties() const override;
	virtual void MakeToolView() override;
	//~End SMetaHumanCharacterEditorToolView interface

	//~Begin FNotifyHook interface
	virtual void NotifyPreChange(FProperty* PropertyAboutToChange) override;
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;
	//~End FNotifyHook interface

private:

	/** Creates the details view section for the export properties. */
	TSharedRef<SWidget> CreateExportToolViewDetailsViewSection();

	/** Creates the section widget for showing the Export button. */
	TSharedRef<SWidget> CreateExportToolViewExportSection();

	/** True if the Export button is enabled. */
	bool IsExportButtonEnabled() const;

	/** Called when the Export button is clicked. */
	FReply OnExportButtonClicked() const;

	/** Gets the warning message visibility. */
	EVisibility GetWarningVisibility() const;

	/** The optional error message displayed when export cannot proceed. */
	mutable FText ExportErrorMsg;

	/** Reference to the Details View that displays export properties. */
	TSharedPtr<IDetailsView> DetailsView;
};
