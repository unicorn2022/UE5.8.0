// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StatusBarSubsystem.h"
#include "Toolkits/BaseToolkit.h"
#include "UI/Widgets/SMetaHumanCharacterEditorAssetViewsPanel.h"

enum class EToolShutdownType : uint8;
class FMetaHumanCurveDataController;
class SEditorViewport;

/**
 * The Mode toolkit is responsible for the panel on the side in the asset editor
 * that shows mode and tool properties. Tool buttons would go in Init().
 * It also builds the toolbar used in the asset editor
 */
class FMetaHumanCharacterEditorModeToolkit : public FModeToolkit
{
public:
	/** Constructor. */
	FMetaHumanCharacterEditorModeToolkit();

	// ~Begin FModeToolkit interface
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode) override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FText GetActiveToolDisplayName() const override;
	virtual TSharedPtr<class SWidget> GetInlineContent() const override;
	virtual void OnToolStarted(UInteractiveToolManager* InManager, UInteractiveTool* InTool) override;
	virtual void OnToolEnded(UInteractiveToolManager* InManager, UInteractiveTool* InTool) override;
	// ~End FModeToolkit interface

	/** Set/Clear notification messages displayed in the status bar. */
	virtual void PostNotification(const FText& InMessage);
	virtual void ClearNotification();

	/** Set/Clear warnings messages. */
	virtual void PostWarning(const FText& InMessage);
	virtual void ClearWarning();

	/** Updates warning text dependent on rigging state and if downloading textures*/
	void UpdateWarningText();

private:

	/* Getters */
	TSharedPtr<SEditorViewport> GetEditorViewport() const;
	EVisibility GetCustomWarningVisibility() const;
	bool IsViewportModeVisible() const;
	FText GetCustomWarning() const;

	/** Creates the toolbar widget used to display the subtools of the given tool. */
	TSharedRef<SWidget> CreateSubToolsToolbar(TNotNull<UInteractiveTool*> Tool, bool bUseLabel = true) const;

	/** Creates the view used to display the current active tool. */
	TSharedRef<SWidget> CreateToolView(UInteractiveTool* Tool);

	/** Creates a details view used to display the current active tool. */
	TSharedRef<IDetailsView> CreateToolDetailsView(UInteractiveTool* Tool) const;

	/** Creates the widget used to contain the active tool view. */
	TSharedRef<SWidget> MakeActiveToolViewWidget();

	/** Creates the widget used to display custom warnings. */
	TSharedRef<SWidget> MakeCustomWarningsWidget();

	/** Creates the tool palettes and register them in the FToolkitBuilder.  */
	void RegisterPalettes();
	
	/** Initializes the viewport widget. */
	void InitializeViewport();

	/** Updates the viewport according to the active tool. */
	void UpdateViewport(UInteractiveTool* Tool, bool bOnToolEnded = false);

	/** Updates the active tool view widget according to the active tool. */
	void UpdateActiveToolViewWidget();

	/** Updates the subtools toolbar according to the active tool. */
	void UpdateSubToolsToolbar();

	/** Updates the active tool view status according to the active tool. */
	void UpdateActiveToolViewStatus();

	/** Updates the tracker image viewer in the viewport overlay. */
	void UpdateTrackerImageViewer();

	/** Handles the activation of auto-activating tools. */
	void HandleAutoActivatingTools();

	/** Handles the recovery of the last tool activation. */
	void HandleLastToolActivation(UInteractiveTool* Tool);

	/** Handles the shutdown of a tool. */
	void HandleToolShutdown();

	/** Checks if a tool can be activated after checking the body state. */
	bool CanActivateToolAfterBodyStateCheck();

	/** Called when the active palette has been changed. */
	void OnActivePaletteChanged();

	/** Called when the property set of a tool with subtools gets modified. */
	void OnSubToolPropertySetsModified(UInteractiveTool* Tool, const FName ToolName);

	/** Called when the editor viewport widget is resized */
	void OnViewportResized(FVector2D InSize);

	/** Called when the visibility of the tracker image viewer changes. */
	void OnTrackerImageVisibilityChanged(bool bIsVisible);

	/** Returns the size of the tracker image viewer in the viewport overlay. */
	FVector2D GetTrackerViewerImageSize() const;

	/** Returns true if the viewport should display the tracking points of the active tool. */
	bool ShouldDrawTrackingPointsOnViewport() const;

private:
	/** Widget used to display warning messages raised by tools */
	TSharedPtr<STextBlock> ToolWarningArea;

	/** The name of the active tool */
	FText ActiveToolName;

	/** The icon of the active tool, used in the Accept/Cancel widget */
	const FSlateBrush* ActiveToolIcon = nullptr;

	/** Handle of the message being displayed in the toolkit status bar */
	FStatusBarMessageHandle ActiveToolMessageHandle;

	/** Contains the widget container for the SubTools Toolbar */
	TSharedPtr<SWidget> SubToolsToolbarWidget;

	/** Contains the widget container for the Active Tool view */
	TSharedPtr<SWidget> ActiveToolViewWidget;

	/** Contains the Active Tool view */
	TSharedPtr<SWidget> ActiveToolView;

	/** Contains the overlay for the viewport */
	TSharedPtr<SWidget> ViewportOverlayWidget;

	/** The map used for remembering the last active tool of a mode */
	TMap<FName, FName> ModeNameToLastActiveToolNameMap;

	/** The map used for remembering the last active subtool of a tool */
	TMap<FName, FName> ToolNameToLastActiveSubToolNameMap;

	/** The map used for remembering the last scroll offset tool of a tool */
	TMap<FName, float> ToolNameToLastScrollOffsetMap;

	/** The map used for remembering the status of the asset views panels in the tool views */
	TMap<FName, FMetaHumanCharacterAssetViewsPanelStatus> ToolViewNameToStatusMap;

	/** The map used for remembering the status of the asset views in the tool views */
	TMap<FName, TArray<FMetaHumanCharacterAssetViewStatus>> ToolViewNameToStatusArrayMap;

	/** Custom warning */
	FText CustomWarning;

	/** True if the toolkit is displaying a tool view widget */
	bool bHasToolView = false;
};