// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericQuadTree.h"
#include "Framework/SlateDelegates.h"
#include "Widgets/SCompoundWidget.h"

class FUICommandList;
class FChaosVDScene;
class SCVDMetricsHeatmapToolbar;
class FChaosVDMetricsViewerState;
struct FParticleMetricEntry;

struct SCVDMetricsHeatmapState
{
	FBox2f ScreenRect = FBox2f(ForceInit);

	/** Center focus point in world space */
	FVector2D FocusCenter = FVector2D::ZeroVector;

	/** Transform from world to screen space */
	FTransform2d WorldToScreen;

	/** Transform from screen to world space */
	FTransform2d ScreenToWorld;

	static constexpr float HeatmapMinScale = 0.0016f;
	static constexpr float HeatmapMaxScale = 0.05f;

	/** Current zoom scale */
	float Scale = HeatmapMaxScale;
};

struct SCVDMetricsHeatmapCellData{
	int64 XCoord;
	int64 YCoord;
	bool bIsCalculated = false;
	double CalculatedMetric = 0;
};

/** Heatmap view based on the FN Spatial Profiler */
class SCVDMetricsHeatmapView : public SCompoundWidget
{
	SLATE_DECLARE_WIDGET(SCVDMetricsHeatmapView, SCompoundWidget);

public:
	SLATE_BEGIN_ARGS(SCVDMetricsHeatmapView) {}
	SLATE_END_ARGS()

	SCVDMetricsHeatmapView();

	void Construct(const FArguments& InArgs, const TWeakPtr<FChaosVDScene> InCVDScene, TSharedPtr<FChaosVDMetricsViewerState> InViewerState);

	void RefreshHeatmapView(bool bInvalidateBounds = true);

	void FocusEditorView() const;
	void FocusSampleBounds();

	void Teleport();

private:
	void BindCommands();
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	void UpdateMouseByEvent(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	void UpdateTransform() const;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	int32 PaintGrid(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	int32 PaintSpatialCell(const SCVDMetricsHeatmapCellData& Cell, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	int32 PaintEditorView(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	int32 PaintSpatialData(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	int32 PaintSelectionBounds(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	int32 PaintAxes(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	int32 PaintLegend(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	void UpdateSelectionBox();

	bool IsScreenRectValid() const;
	bool IsMouseDragging() const;

	mutable SCVDMetricsHeatmapState State;

	FVector2f CursorScreenSpacePosition = FVector2f(ForceInitToZero);
	FVector2D CursorWorldPosition = FVector2D(ForceInitToZero);
	FVector2D LastCursorWorldDragPosition = FVector2D(ForceInitToZero);

	FVector2D SelectionStart = FVector2D(ForceInitToZero);
	FVector2D SelectionEnd = FVector2D(ForceInitToZero);

	int32 HeatmapCellSize = 200;

	float TotalMouseDelta = 0.0f;

	bool bIsTrackingCamera = false;

	bool bIsPanning = false;
	bool bIsFiltering = false;

	TSharedPtr<FChaosVDMetricsViewerState> ViewerState;
	TWeakPtr<FChaosVDScene> CVDScene;

	mutable TArray<SCVDMetricsHeatmapCellData> CellData;
	mutable TSet<FVector2d> CalculatedCells;

	TSharedPtr<SCVDMetricsHeatmapToolbar> Toolbar;
	const TSharedRef<FUICommandList> CommandList;
	TSharedPtr<TQuadTree<int32, 16>> QueuedQuadTree;
	TUniquePtr<TQuadTree<int32, 16>> QuadTree;
};