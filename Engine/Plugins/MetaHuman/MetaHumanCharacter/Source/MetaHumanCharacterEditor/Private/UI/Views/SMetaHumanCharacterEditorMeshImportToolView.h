// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/NotifyHook.h"
#include "SMetaHumanCharacterEditorToolView.h"

enum class EMetaHumanMeshImportMode : uint8;
class FReply;
struct FSlateBrush;
class IDetailsView;
class SWindow;
class UMetaHumanCharacterEditorMeshImportTool;

/** View for displaying the Mesh Import Tool in the MetaHumanCharacter editor */
class SMetaHumanCharacterEditorMeshImportToolView
	: public SMetaHumanCharacterEditorToolView
	, public FNotifyHook
{
public:
	SLATE_BEGIN_ARGS(SMetaHumanCharacterEditorToolView) {}
	SLATE_END_ARGS()

	/* Destructor. */
	~SMetaHumanCharacterEditorMeshImportToolView();

	/** Constructs the widget. */
	void Construct(const FArguments& InArgs, UMetaHumanCharacterEditorMeshImportTool* InTool);

protected:
	//~ Begin SMetaHumanCharacterEditorToolView interface
	virtual UInteractiveToolPropertySet* GetToolProperties() const override;
	virtual void MakeToolView() override;
	//~ End of SMetaHumanCharacterEditorToolView interface

	//~ Begin FNotifyHook interface
	virtual void NotifyPreChange(FProperty* PropertyAboutToChange) override;
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;
	//~ End of FNotifyHook interface

private:
	/** Creates the section widget for showing the toolbar */
	TSharedRef<SWidget> CreateMeshImportToolbarSection();

	/** Creates the section widget for showing the warning text */
	TSharedRef<SWidget> CreateMeshImportWarningTextSection();

	/** Creates the section widget for showing the Load Custom Mesh properties */
	TSharedRef<SWidget> CreateMeshImportLoadCustomMeshSection();

	/** Creates the section widget for showing the Load Mesh Parts properties */
	TSharedRef<SWidget> CreateMeshImportLoadMeshPartsSection();

	/** Creates the section widget for showing the Custom Mesh properties */
	TSharedRef<SWidget> CreateMeshImportCustomMeshSection();

	/** Creates the section widget for showing the MetaHuman Mesh properties */
	TSharedRef<SWidget> CreateMeshImportMetaHumanMeshSection();

	/** Creates the section widget for showing the Key Points properties */
	TSharedRef<SWidget> CreateMeshImportKeyPointsSection();

	/** Creates the section widget for showing the Facial Tracking properties */
	TSharedRef<SWidget> CreateMeshImportFacialTrackingSection();

	/** Creates the section widget for showing the Import button section. */
	TSharedRef<SWidget> CreateMeshImportButtonSection();

	/** Creates the window content for showing the advanced parameters properties. */
	TSharedRef<SWidget> CreateAdvancedPropertiesWindow();

	/** Helper function to create a button for the mesh import tool. */
	TSharedRef<SWidget> CreateMeshImportToolButton(const FText& ButtonText, const FText& ToolTipText, const FOnClicked& OnClickedHandler, TAttribute<bool> IsEnabled, const FSlateBrush* Icon = nullptr) const;

	/** Called when the advanced features window is closed. */
	void OnAdvancedFeaturesWindowClosed(const TSharedRef<SWindow>& Window);

	/** Gets the current mesh import mode from the tool properties. */
	EMetaHumanMeshImportMode GetMeshImportMode() const;

	/** Called when the mesh import mode is changed by the user. */
	void OnMeshImportModeChanged(EMetaHumanMeshImportMode Mode);

	/** Called when the Reset Body button is clicked. */
	FReply OnResetBodyButtonClicked();

	/** True if the Reset Body button is enabled. */
	bool IsResetBodyButtonEnabled() const;

	/** Called when the Reset Head button is clicked. */
	FReply OnResetHeadButtonClicked();

	/** True if the Reset Head button is enabled. */
	bool IsResetHeadButtonEnabled() const;

	/** Called when the Save Pose button is clicked. */
	FReply OnSavePoseButtonClicked();

	/** True if the Save Pose button is enabled. */
	bool IsSavePoseButtonEnabled() const;

	/** Called when the Delete Key Points button is clicked. */
	FReply OnDeleteKeyPointsButtonClicked();

	/** True if the Delete Key Points button is enabled. */
	bool IsDeleteKeyPointsButtonEnabled() const;

	/** True if the Trace Facial Features button is enabled. */
	bool IsTraceFacialFeaturesButtonEnabled() const;

	/** True if the Facial Curves editing is enabled. */
	bool IsFacialCurvesEditingEnabled() const;

	/** Called when the Trace Facial Features button is clicked. */
	FReply OnTraceFacialFeaturesButtonClicked();

	/** True if the Delete Face Curves button is enabled. */
	bool IsDeleteFaceCurvesButtonEnabled() const;

	/** Called when the Delete Face Curves button is clicked. */
	FReply OnDeleteFaceCurvesButtonClicked();

	/** True if the Align Head button is enabled. */
	bool IsAlignHeadButtonEnabled() const;

	/** Called when the Align Head button is clicked. */
	FReply OnAlignHeadButtonClicked();

	/** True if the Body Solve Step button is enabled. */
	bool IsBodySolveStepButtonEnabled() const;

	/** Called when the Body Solve Step button is clicked. */
	FReply OnBodySolveStepButtonClicked();

	/** True if the Face Solve Step button is enabled. */
	bool IsFaceSolveStepButtonEnabled() const;

	/** Called when the Face Solve Step button is clicked. */
	FReply OnFaceSolveStepButtonClicked();

	/** True if the Refine Vertices button is enabled. */
	bool IsRefineVerticesButtonEnabled() const;
	
	/** Called when the Refine Vertices button is clicked. */
	FReply OnRefineVerticesButtonClicked();

	/** True if the Import button is enabled. */
	bool IsImportButtonEnabled() const;

	/** Called when the Advanced Features button is clicked. */
	FReply OnAdvancedFeaturesButtonClicked();

	/** Called when the Import button is clicked. */
	FReply OnImportButtonClicked();

	/** Gets the text for the warning panel */
	FText GetWarningText() const;

	/** Gets the visibility according to given the mesh import mode */
	EVisibility GetMeshImportModeVisibility(EMetaHumanMeshImportMode Mode) const;

	/** Reference to the window that displays the advanced parameters properties */
	TSharedPtr<SWindow> AdvancedFeaturesWindow;
};
