// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

// TraceInsights
#include "Insights/ObjectProfiler/IObjectProfilerSession.h"
#include "Insights/Table/Widgets/SSessionTableTreeView.h"

namespace UE::Insights
{
	class SSegmentedBarGraph;
}

namespace UE::Insights::ObjectProfiler
{

class IAssetInfoProvider;
class FObjectNode;
class FObjectTable;
class SObjectDetailsView;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FObjectSnapshotInfo
{
	friend class SObjectTableTreeView;

public:
	FObjectSnapshotInfo() {}

	uint32 GetId() const { return Id; }
	bool HasTotalEstimatedMemory() const { return bHasTotalEstimatedMemory; }

	FText GetName() const;
	FText GetToolTip() const;

private:
	uint32 Id = uint32(-1);
	double StartTime = 0.0;
	double EndTime = 0.0;
	uint32 ObjectCount = 0;
	bool bHasTotalEstimatedMemory = false;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * A custom widget used to display the list of objects from a UObject Snapshot.
 */
class SObjectTableTreeView : public SSessionTableTreeView, public IObjectProfilerSession
{
public:
	/** Default constructor. */
	SObjectTableTreeView();

	/** Virtual destructor. */
	virtual ~SObjectTableTreeView();

	SLATE_BEGIN_ARGS(SObjectTableTreeView) {}
	SLATE_END_ARGS()

	/**
	 * Construct this widget
	 * @param InArgs - The declaration data for this widget
	 */
	void Construct(const FArguments& InArgs);

	virtual void ConstructHeaderArea(TSharedRef<SVerticalBox> InHostBox) override;
	virtual TSharedPtr<SWidget> ConstructFooter() override;

	virtual void Reset();

	TWeakPtr<SSegmentedBarGraph> GetSegmentedBarGraph() const
	{
		return WeakSegmentedBarGraph;
	}

	void SetSegmentedBarGraph(TWeakPtr<SSegmentedBarGraph> InSegmentedBarGraph)
	{
		WeakSegmentedBarGraph = InSegmentedBarGraph;
	}

	TWeakPtr<SObjectDetailsView> GetObjectDetailsView() const
	{
		return WeakObjectDetailsView;
	}

	void SetObjectDetailsView(TWeakPtr<SObjectDetailsView> InObjectDetailsView)
	{
		WeakObjectDetailsView = InObjectDetailsView;
	}

	/**
	 * Ticks this widget.  Override in derived classes, but always call the parent implementation.
	 *
	 * @param  AllottedGeometry The space allotted for this widget
	 * @param  InCurrentTime  Current absolute real time
	 * @param  InDeltaTime  Real time passed since last tick
	 */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/**
	 * Rebuilds the tree (if necessary).
	 * @param bResync - If true, it forces a resync
	 */
	virtual void RebuildTree(bool bResync);

	TSharedPtr<IAssetInfoProvider> GetAssetInfoProvider() const
	{
		return WeakAssetInfoProvider.Pin();
	}

	void SetAssetInfoProvider(TSharedPtr<IAssetInfoProvider> InAssetInfoProvider)
	{
		WeakAssetInfoProvider = InAssetInfoProvider.ToWeakPtr();
	}

	TSharedPtr<FObjectNode> GetObjectNode(uint32 ObjectId);
	void SelectObjectNode(uint32 ObjectId);

	// IObjectProfilerSession
	virtual bool IsHideObjectsExternalToProject() const override { return bHideObjectsExternalToProject; }
	virtual void SetHideObjectsExternalToProject(bool bHide) override;
	// End IObjectProfilerSession

	bool HasTotalEstimatedMemory() const { return bHasTotalEstimatedMemory; }

	void PostApplyAssetsViewPreset();

protected:
	virtual void InternalCreateGroupings() override;

	virtual void ExtendMenu(TSharedRef<FExtender> Extender) override;
	void ExtendMenuBeforeMisc(FMenuBuilder& MenuBuilder);
	void ExtendMenuAfterMisc(FMenuBuilder& MenuBuilder);

	virtual bool HasCustomNodeFilter() const override;
	virtual bool FilterNodeCustom(const FTableTreeNode& InNode) const override;

	virtual void TreeView_OnMouseButtonDoubleClick(FTableTreeNodePtr NodePtr) override;
	virtual void TreeView_OnSelectionChanged(FTableTreeNodePtr SelectedItem, ESelectInfo::Type SelectInfo) override;

	virtual void UpdateBannerText() override;

private:
	void UpdateAvailableSnapshots();
	void ConstructSnapshotSelector(TSharedPtr<SHorizontalBox> Box);
	void Snapshot_OnSelectionChanged(TSharedPtr<FObjectSnapshotInfo> InSnapshot, ESelectInfo::Type SelectInfo);
	TSharedRef<SWidget> Snapshot_OnGenerateWidget(TSharedRef<FObjectSnapshotInfo> InSnapshot);
	FText Snapshot_GetSelectedText() const;
	FText Snapshot_GetSelectedToolTipText() const;

	TSharedRef<SWidget> MakeFiltersMenu();

	bool IsObjectDetailsViewVisible() const;
	void ToggleObjectDetailsViewVisibility();

	void UpdateSelectionStatsText();
	FText GetNumSelectedObjectsText() const { return NumSelectedObjectsText; }
	FText GetSelectedObjectsText() const { return  SelectedObjectsText; }
	FText GetSelectionStructSizeText() const { return SelectionStructSizeText; }
	FText GetSelectionSystemMemSizeText() const { return SelectionSystemMemSizeText; }
	FText GetSelectionVideoMemSizeText() const { return SelectionVideoMemSizeText; }

	void InitAvailableViewPresets();

private:
	TSharedPtr<SComboBox<TSharedRef<FObjectSnapshotInfo>>> SnapshotComboBox;
	TArray<TSharedRef<FObjectSnapshotInfo>> AvailableSnapshots;
	TSharedPtr<FObjectSnapshotInfo> SelectedSnapshot;
	static constexpr uint32 InvalidSnapshotId = uint32(-1);
	uint32 CurrentSnapshotId = InvalidSnapshotId;

	TWeakPtr<IAssetInfoProvider> WeakAssetInfoProvider;

	TArray<TSharedPtr<FObjectNode>> ObjectNodes;
	TArray<TSharedPtr<FObjectNode>> Classes;
	TArray<TSharedPtr<FObjectNode>> Packages;

	//////////////////////////////////////////////////
	// Filters

	bool bHideObjectsWithZeroReferencingActors = false; // hide objects that are not referenced by any Actor

	bool bHideObjectsWithZeroEstimatedMemory = false; // hide objects with no Estimated Memory (System + Video)
	bool bHideObjectsWithLowEstimatedMemory = false; // hide objects with Estimated Memory (System + Video) < 1 MiB
	bool bHideObjectsWithLowImpact = false; // hide objects with Impact % < 1%

	bool bHideObjectsWithZeroTotalEstimatedMemory = false; // hide objects with no Total Estimated Memory (System + Video)
	bool bHideObjectsWithLowTotalEstimatedMemory = false; // hide objects with Total Estimated Memory (System + Video) < 1 MiB
	bool bHideObjectsWithLowTotalImpact = false; // hide objects with Total Impact % < 1%

	bool bHidePackages = false; // hide packages
	bool bHideSubObjects = false; // hide sub-objects

	bool bHideObjectsExternalToProject = false; // UEFN: hide objects whose matched asset is not under the user's currently-opened project mount points or who don't have a matching asset

	bool bFilterByClassType = false;
	bool bShowOnlyFieldObjects = false; // shows only the UField objects (including UStruct objects)
	bool bShowOnlyStructObjects = false; // shows only the UStruct objects (including UClass and UFunction objects)
	bool bShowOnlyClassObjects = false; // shows only the UClass objects
	bool bShowOnlyFunctionObjects = false; // shows only the UFunction objects
	bool bShowOnlyPackageObjects = false; // shows only the UPackage objects

	bool bCanShowReferencingActors = false;

	bool bIsSimplifiedMode = false;
	bool bShowAdvancedUI = false;
	bool bIsViewPresetsDropDownVisible = false;
	bool bIsAdvancedFilterConfiguratorVisible = false;
	bool bIsHierarchyBreadcrumbTrailVisible = false;

	bool bHasTotalEstimatedMemory = false;

	//////////////////////////////////////////////////

	TWeakPtr<SSegmentedBarGraph> WeakSegmentedBarGraph;
	TWeakPtr<SObjectDetailsView> WeakObjectDetailsView;

	FText NumSelectedObjectsText;
	FText SelectedObjectsText;
	FText SelectionStructSizeText;
	FText SelectionSystemMemSizeText;
	FText SelectionVideoMemSizeText;

	uint64 NextTimestamp = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::ObjectProfiler
