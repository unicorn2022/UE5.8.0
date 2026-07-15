// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderAuditTypes.h"
#include "ShaderAuditSession.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Navigation/SBreadcrumbTrail.h"

class FMenuBuilder;
class STreeMap;
class SHeaderRow;
class FTreeMapNodeData;
template <typename ItemType> class STreeView;
template <typename ItemType> class SListView;

/** Caller-supplied hook to add extra entries to a material asset's right-click context menu.
 *  If unbound, the menu just contains the widget's built-in entries (e.g., copy hash). */
DECLARE_DELEGATE_TwoParams(FOnExtendShaderAssetContextMenu, FMenuBuilder& /*MenuBuilder*/, const FString& /*AssetPath*/);

/** Caller-supplied hook to "open this asset in the content browser" (or equivalent navigation).
 *  If unbound, the widget logs a notice and does nothing -- standalone mode. */
DECLARE_DELEGATE_OneParam(FOnOpenShaderAssetInContentBrowser, const FString& /*AssetPath*/);

/**
 * Interactive shader cost treemap with folder navigation and detail panel.
 * Layout: Left = folder tree, Right-top = detail panel, Right-bottom = STreeMap treemap.
 *
 * Data model: FShaderFolderNode tree is the stable, persistent hierarchy.
 * FTreeMapNodeData trees are throwaway views rebuilt on every navigation.
 */
class SShaderCostTreeMap : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShaderCostTreeMap) {}
		/** Optional hook: invoked when building the right-click context menu for a material asset.
		 *  Lets the editor add entries (e.g., "Find Similar Instances") without the widget
		 *  itself depending on editor-only modules. */
		SLATE_EVENT(FOnExtendShaderAssetContextMenu, OnExtendAssetContextMenu)

		/** Optional hook: invoked when the user requests "open in content browser" from the
		 *  detail panel. If unbound, the widget logs and falls through. */
		SLATE_EVENT(FOnOpenShaderAssetInContentBrowser, OnOpenAssetInContentBrowser)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Set the session to visualize. Call after loading an SHK file. */
	void SetSession(TSharedPtr<FShaderAuditSession> InSession);

	/** Clear all data */
	void ClearData();

	/** Current navigation path (e.g. "/Game/Characters"). "/" = root. */
	const FString& GetCurrentPath() const { return NavigationHistory[HistoryIndex]; }

	/** Current visibility bit array. One bit per StableShaderKeyAndValueArray entry. */
	const TBitArray<>& GetVisibleShaders() const { return VisibleShaders; }

	// SWidget overrides
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	/** Rebuild the folder/hierarchy tree from current filters. */
	void RebuildFromFilters();

	bool IsMaterialHierarchyMode() const;

private:
	// Layout builders (in SShaderCostTreeMap_Construct.cpp)
	TSharedRef<SWidget> MakeToolbar();
	TSharedRef<SWidget> MakeFilterBar();
	TSharedRef<SWidget> MakeFilterTagBar();
	TSharedRef<SWidget> MakePathBar();
	TSharedRef<SWidget> MakeContentArea();

	// --- Data ---
	TSharedPtr<FShaderAuditSession> Session;
	TBitArray<> VisibleShaders;					// One bit per StableShaderKeyAndValueArray entry (all true = no filter)
	TSharedPtr<FShaderFolderNode> RootFolderData;	// Cached folder tree (rebuilt when filters change)
	TMap<FString, TSharedPtr<FShaderFolderNode>> FolderNodeMap;
	TSharedPtr<FTreeMapNodeData> RootTreeMapNode;

	// --- State ---
	int32 MaxRefCount = 1; // 0 = no filter, default 1 = unique shaders only
	int32 MaxTreeDepth = 3; // depth limit, clamped to [1,20] by UI (GetDepthLabel handles 0 defensively)
	enum class ETreeMapMode : uint8
	{
		FolderTree,
		MaterialHierarchy,
	};
	ETreeMapMode TreeMapMode = ETreeMapMode::FolderTree;

	bool bIsNavigating = false; // Guard against re-entrant navigation

	// --- History ---
	TArray<FString> NavigationHistory = { TEXT("/") };
	int32 HistoryIndex = 0;
	bool bIsHistoryNavigation = false;

	// --- Folder Tree (sidebar) ---
	TArray<TSharedPtr<FShaderFolderNode>> SidebarRootNodes; // Top-level folders for the STreeView
	TSharedPtr<STreeView<TSharedPtr<FShaderFolderNode>>> FolderTreeWidget;

	void RebuildFolderTree();
	TSharedRef<class ITableRow> OnGenerateFolderRow(TSharedPtr<FShaderFolderNode> Item, const TSharedRef<class STableViewBase>& OwnerTable);
	void OnGetFolderChildren(TSharedPtr<FShaderFolderNode> Item, TArray<TSharedPtr<FShaderFolderNode>>& OutChildren);
	void OnFolderSelectionChanged(TSharedPtr<FShaderFolderNode> Item, ESelectInfo::Type SelectInfo);

	// --- Treemap ---
	TSharedPtr<STreeMap> TreeMapWidget;

	void RebuildTreeMap();
	void OnTreeMapNodeDoubleClicked(FTreeMapNodeData& NodeData, const FPointerEvent& MouseEvent);
	void OnTreeMapNodeRightClicked(FTreeMapNodeData& NodeData, const FPointerEvent& MouseEvent);
	void OnTreeMapWheelNavigate(const FString& TargetPath);

	// --- Detail Panel ---
	TArray<TSharedPtr<FShaderDetailRow>> DetailRows;
	TSharedPtr<class SWidgetSwitcher> DetailSwitcher;
	FText GetDetailTitle() const { return DetailTitle; }
	FText DetailTitle;

	// Folder detail (slot 0): plain STextBlock rows -- Name, Class, Shaders, Cost
	TSharedPtr<SListView<TSharedPtr<FShaderDetailRow> > > FolderDetailListWidget;
	TSharedPtr<SHeaderRow> FolderDetailHeaderRow;
	TSharedRef<class ITableRow> OnGenerateFolderDetailRow(TSharedPtr<FShaderDetailRow> Item, const TSharedRef<class STableViewBase>& OwnerTable);

	// Shader detail (slot 1): selectable SEditableText rows -- ShaderType, VFType, Perm, Hash, RefCount, Size
	TSharedPtr<SListView<TSharedPtr<FShaderDetailRow> > > ShaderDetailListWidget;
	TSharedPtr<SHeaderRow> ShaderDetailHeaderRow;
	TSharedRef<class ITableRow> OnGenerateShaderDetailRow(TSharedPtr<FShaderDetailRow> Item, const TSharedRef<class STableViewBase>& OwnerTable);

	void RefreshDetailPanel(const FString& CurrentPath);
	void ShowFolderDetail(const FString& FolderPath);
	void ShowAssetDetail(int32 MaterialIndex);
	void OnDetailRowDoubleClicked(TSharedPtr<FShaderDetailRow> Item);
	void OpenAssetInContentBrowser(const FString& AssetPath);

	// --- Editor hooks (bound by caller via Slate args) ---
	FOnExtendShaderAssetContextMenu OnExtendAssetContextMenuHook;
	FOnOpenShaderAssetInContentBrowser OnOpenAssetInContentBrowserHook;

	/** Build the right-click context menu for a material asset node. Used by both the
	 *  treemap right-click handler and the folder tree context menu.
	 *  Returns null if the node has neither a valid MaterialIndex nor an EditorPath. */
	TSharedPtr<SWidget> BuildAssetContextMenu(const TSharedPtr<FShaderFolderNode>& Node);

	// --- Navigation ---
	void NavigateTo(const FString& Path);
	void ExpandFolderTreeTo(const FString& FolderPath);

	// --- Breadcrumb / Path Edit ---
	TSharedPtr<SBreadcrumbTrail<FString>> BreadcrumbTrail;
	TSharedPtr<class SWidgetSwitcher> PathSwitcher;
	TSharedPtr<class SEditableTextBox> PathEditBox;
	void RebuildBreadcrumb();
	void OnBreadcrumbClicked(const FString& Path);
	void StartPathEdit();
	void OnPathEditCommitted(const FText& NewText, ETextCommit::Type CommitType);

	// --- Filters ---
	TArray<FShaderFilterNode> ActiveFilters;
	TArray<FString> ActiveFilterStrings;
	TSharedPtr<class SWrapBox> FilterTagBox;
	TSharedPtr<class SSearchBox> HashSearchBox;
	void OnFilterCommitted(const FText& Text, ETextCommit::Type CommitType);
	void RemoveFilter(int32 Index);
	void ApplyFilters();
	void RebuildFilterTags();
	void GetFilterSuggestions(const FString& Text, TArray<FString>& OutSuggestions);

	// --- Refcount ---
	TOptional<int32> GetRefCountValue() const { return MaxRefCount; }
	void OnRefCountCommitted(int32 NewValue, ETextCommit::Type CommitType);
	FText GetRefCountLabel() const;
	FText GetStatsLabel() const;
	FText CachedStatsLabel;
	void UpdateCachedStats();

	// --- Depth ---
	TOptional<int32> GetDepthValue() const { return MaxTreeDepth; }
	void OnDepthCommitted(int32 NewValue, ETextCommit::Type CommitType);
	FText GetDepthLabel() const;
};
