// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SAssetEditorViewport.h"

#include "Framework/SlateDelegates.h"
#include "MetaHumanCharacterViewport.h"
#include "Slate/SceneViewport.h"
#include "SMetaHumanOverlayWidget.h"
#include "STrackerImageViewer.h"
#include "UObject/GCObject.h"
 
struct FSlateBrush;
class UInteractiveToolManager;

class SMetaHumanCharacterEditorViewport : public SAssetEditorViewport, public FGCObject
{
public:

	/* Constructor */
	void Construct(const FArguments& InArgs, const FAssetEditorViewportConstructionArgs& InViewportConstructionArgs);

	/* Destructor - destroys the scene capture component so it doesn't outlive its world. */
	virtual ~SMetaHumanCharacterEditorViewport();

	/** Returns a reference to the viewport client that controls this viewport */
	TSharedRef<class FMetaHumanCharacterViewportClient> GetMetaHumanCharacterEditorViewportClient() const;

	/** Returns a reference to the STrackerImageViewer we are controlling */
	TSharedRef<SMetaHumanOverlayWidget<STrackerImageViewer>> GetTrackerImageViewer() const;

	/** Gets or creates a render target texture to be used for tracking the viewport in the 2D View Overlay widget */
	UTextureRenderTarget2D* GetOrCreateTrackingTexture(UObject* WorldContextObject, UTexture* SourceTexture, const FIntPoint& SourceSize, const FIntPoint& TargetSize);

	/** Sets the texture to be displayed in the tracker image viewer overlay */
	void SetTrackerImageTexture(UTexture* InTexture, const FIntPoint& InImageSize);

	/** Returns the current geometry of the viewport widget */
	FGeometry GetCurrentViewportGeometry() const { return CurrentViewportGeometry; }

	/** Sets the visibility of the viewport mode toolbar buttons */
	void SetViewportModeVisible(TAttribute<bool> IsVisible) { IsViewportModeVisibleAttribute = IsVisible; }

	/** True if the viewport mode toolbar buttons are visible */
	bool IsViewportModeVisible() const;

	/** Toggles the visibility state of the 2D View Overlay widget */
	void Toggle2DViewOverlay() { bIs2DViewOverlayEnabled = !bIs2DViewOverlayEnabled; }

	/** Gets the visibility state of the 2D View Overlay widget */
	bool Is2DViewOverlayEnabled() const { return bIs2DViewOverlayEnabled; }

	/** Updates the 2D View Overlay widget based on the current state of the viewport */
	void Update2DViewportOverlay(bool bEnable2DView);

	//~Begin SAssetEditorViewport interface
	virtual void Tick(const FGeometry& InAllottedGeometry, const double InCurrentTime, const float InDeltaTime);
	virtual void PopulateViewportOverlays(TSharedRef<SOverlay> InOverlay) override;
	virtual TSharedPtr<SWidget> BuildViewportToolbar() override;
	virtual void BindCommands() override;
	//~End SAssetEditorViewport interface

	//~Begin FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;
	//~End FGCObject interface

	/** Delegate used to trigger when the viewport size changes, which is used to update the tracker image viewer */
	FOnSceneViewportResize OnViewportSizeChangedDelegate;

	/** Delegate used to determine whether or not to draw the tracking points overlay */
	FIsSelected OnShouldDrawTrackingPointsDelegate;

	/** Delegate used to trigger when the visibility of the tracker image viewer changes, which is used to update the tracker image viewer */
	FOnBooleanValueChanged OnTrackerImageVisibilityChangedDelegate;

	/** A reference to the custom environment selection combo button in the viewport toolbar */
	TSharedPtr<class SComboButton> CustomEnvironmentSelectionBox;

private:

	/** Initializes the scene capture component used to track the viewport for the 2D View Overlay widget */
	void InitializeSceneCaptureComponent();

	/** Captures the current view of the viewport */
	void CaptureCurrentView();

	/** Computes the rectangle to fit the source texture into the target size while maintaining aspect ratio */
	FBox2D ComputeTextureFitRect(const FIntPoint& SourceSize, const FIntPoint& TargetSize) const;

	/* Returns a pointer to the InteractiveToolManager from the current ToolsContext, or nullptr if it is not available */
	UInteractiveToolManager* GetToolManager() const;

	/** Updates the brush used to draw the current viewport capture in the 2D View Overlay widget */
	void UpdateCurrentViewportImageBrush(UTexture* InTexture, const FIntPoint& InImageSize);

	/** Updates the brush used to draw the tracker image viewer overlay with the given texture and image size */
	void UpdateTrackerImageBrush(UTexture* InTexture, const FIntPoint& InImageSize);

	/** Gets the label text for the 2D View Overlay widget based on the current state of the viewport */
	FText Get2DViewportModeOverlayLabelText() const;

	/** Gets the brush used to draw the current viewport capture in the 2D View Overlay widget */
	const FSlateBrush* Get2DViewportModeOverlayImageBrush() const;

	/** Gets the visibility of the 2D View Overlay widget */
	EVisibility	Get2DViewportModeOverlayVisibility() const;

	/** Called when the 2D View Overlay widget is clicked, toggles the visibility of the overlay */
	FReply On2DViewportModeOverlayClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

	/** Focuses the camera on the given frame */
	void FocusOnFrame(EMetaHumanCharacterCameraFrame FrameToFocus);

	/** Returns true if the given camera frame is currently selected in the viewport */
	bool IsCameraFrameChecked(EMetaHumanCharacterCameraFrame Frame) const;

	/** Returns true if the tracking points overlay should be drawn */
	bool ShouldDrawTrackingPoints() const;

	/** A reference to the tracker image viewer overlay */
	TSharedPtr<SMetaHumanOverlayWidget<STrackerImageViewer>> TrackerImageViewer;

	/** The brush used to draw the current viewport capture in the 2D View Overlay widget */
	TSharedPtr<FSlateBrush> CurrentViewportBrush;

	/** The brush used to draw the tracker image viewer overlay */
	TSharedPtr<FSlateBrush> TrackerImageViewerBrush;

	/** A pointer to the scene capture component used for tracking the viewport */
	TObjectPtr<class USceneCaptureComponent2D> SceneCaptureComponent;

	/** A pointer to the render target used to capture the viewport for the 2D View Overlay widget */
	TObjectPtr<class UTextureRenderTarget2D> TrackingRenderTarget;

	/** An attribute used to determine the visibility of the viewport mode toolbar buttons */
	TAttribute<bool> IsViewportModeVisibleAttribute;

	/** Holds the current geometry of the widget. Used to trigger the ViewportSizeChangedDelegate if the size changes */
	FGeometry CurrentViewportGeometry;

	/** True if the 2D View Overlay widget is enabled */
	bool bIs2DViewOverlayEnabled = true;
};