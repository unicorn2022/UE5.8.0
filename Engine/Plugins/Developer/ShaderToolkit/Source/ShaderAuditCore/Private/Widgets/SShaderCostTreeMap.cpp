// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SShaderCostTreeMap.h"
#include "ShaderAuditSession.h"
#include "ShaderAuditCore.h"
#include "ShaderAuditViews.h"
#include "ShaderBytecodeDatabase.h"
#include "ShaderAuditTypes.h"
#include "ITreeMap.h"
#include "STreeMap.h"

#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Styling/CoreStyle.h"
#include "Styling/AppStyle.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

// ============================================================================
// Multi-column detail row
// ============================================================================

class SShaderDetailTableRow : public SMultiColumnTableRow<TSharedPtr<FShaderDetailRow>>
{
public:
	SLATE_BEGIN_ARGS(SShaderDetailTableRow) {}
		SLATE_ARGUMENT(TSharedPtr<FShaderDetailRow>, Item)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTableView)
	{
		Item = InArgs._Item;
		SMultiColumnTableRow<TSharedPtr<FShaderDetailRow>>::Construct(
			FSuperRowType::FArguments(),
			OwnerTableView);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		auto MakeSelectableText = [](const FText& InText, const FText& InToolTip = FText::GetEmpty()) -> TSharedRef<SWidget>
		{
			return SNew(SEditableText)
				.Text(InText)
				.ToolTipText(InToolTip.IsEmpty() ? InText : InToolTip)
				.IsReadOnly(true)
				.Style(&FCoreStyle::Get().GetWidgetStyle<FEditableTextStyle>("NormalEditableTextStyle"));
		};

		if (ColumnName == "ShaderType")
		{
			return MakeSelectableText(FText::FromName(Item->ShaderType));
		}
		else if (ColumnName == "VFType")
		{
			return MakeSelectableText(FText::FromName(Item->VFType));
		}
		else if (ColumnName == "Perm")
		{
			return MakeSelectableText(FText::FromName(Item->PermutationId));
		}
		else if (ColumnName == "Hash")
		{
			return MakeSelectableText(FText::FromString(Item->HashString));
		}
		else if (ColumnName == "RefCount")
		{
			return MakeSelectableText(Item->RefCount > 0 ? FText::AsNumber(Item->RefCount) : FText::GetEmpty());
		}
		else if (ColumnName == "Size")
		{
			return MakeSelectableText(Item->Size > 0 ? FText::FromString(UE::ShaderAudit::Utils::FormatBytes(static_cast<uint64>(Item->Size))) : FText::GetEmpty());
		}
		else if (ColumnName == "Name")
		{
			return MakeSelectableText(
				FText::FromString(Item->Label),
				FText::FromString(Item->AssetPath.IsEmpty() ? Item->HashString : Item->AssetPath));
		}
		else if (ColumnName == "Class")
		{
			return MakeSelectableText(FText::FromString(Item->ClassName));
		}
		else if (ColumnName == "Shaders")
		{
			return MakeSelectableText(FText::AsNumber(Item->ShaderCount));
		}
		else if (ColumnName == "Cost")
		{
			return MakeSelectableText(Item->Cost > 0.f ? FText::FromString(UE::ShaderAudit::Utils::FormatCost(Item->Cost, Item->bSizeWeighted)) : FText::GetEmpty());
		}

		return SNullWidget::NullWidget;
	}

private:
	TSharedPtr<FShaderDetailRow> Item;
};
#define LOCTEXT_NAMESPACE "ShaderCostTreeMap"


// ============================================================================
// Data Loading
// ============================================================================

void SShaderCostTreeMap::SetSession(TSharedPtr<FShaderAuditSession> InSession)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SShaderCostTreeMap::SetSession);
	Session = InSession;

	ApplyFilters();
	NavigateTo(TEXT("/"));
}

void SShaderCostTreeMap::ClearData()
{
	Session.Reset();
	VisibleShaders.Empty();
	RootFolderData.Reset();
	FolderNodeMap.Empty();
	SidebarRootNodes.Empty();
	DetailRows.Empty();
	NavigationHistory = { TEXT("/") };
	HistoryIndex = 0;

	// Mutate the root in place -- STreeMap holds a ref to it
	RootTreeMapNode->Children.Empty();
	RootTreeMapNode->Name = TEXT("/");
	RootTreeMapNode->LogicalName.Empty();
	RootTreeMapNode->Size = 0.0f;

	if (TreeMapWidget.IsValid())
	{
		TreeMapWidget->SetTreeRoot(RootTreeMapNode.ToSharedRef(), false);
		TreeMapWidget->RebuildTreeMap(false);
		// STreeMap doesn't self-invalidate; force the enclosing SInvalidationPanel to refresh.
		TreeMapWidget->Invalidate(EInvalidateWidgetReason::Paint);
	}
	if (FolderTreeWidget.IsValid())
	{
		FolderTreeWidget->RequestTreeRefresh();
	}
	if (FolderDetailListWidget.IsValid())
	{
		FolderDetailListWidget->RequestListRefresh();
	}
	if (ShaderDetailListWidget.IsValid())
	{
		ShaderDetailListWidget->RequestListRefresh();
	}
	if (DetailSwitcher.IsValid())
	{
		DetailSwitcher->SetActiveWidgetIndex(0);
	}
}

bool SShaderCostTreeMap::IsMaterialHierarchyMode() const
{
	return TreeMapMode == ETreeMapMode::MaterialHierarchy && Session.IsValid() && Session->HasMaterialHierarchy();
}


// ============================================================================
// Treemap
// ============================================================================

void SShaderCostTreeMap::RebuildFromFilters()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SShaderCostTreeMap::RebuildFromFilters);
	if (!Session.IsValid())
	{
		return;
	}

 	if (IsMaterialHierarchyMode())
	{
		RootFolderData = UE::ShaderAudit::BuildMaterialHierarchyTree(*Session, &VisibleShaders, FolderNodeMap);
	}
	else
	{
		RootFolderData = UE::ShaderAudit::BuildFolderTree(*Session, &VisibleShaders, FolderNodeMap);
	}

	RebuildTreeMap();
	RebuildFolderTree();
	UpdateCachedStats();
	RefreshDetailPanel(GetCurrentPath());
}

void SShaderCostTreeMap::RebuildTreeMap()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SShaderCostTreeMap::RebuildTreeMap);
	if (!RootFolderData.IsValid() || !Session.IsValid())
	{
		return;
	}

	// Find the view root from CurrentPath
	TSharedPtr<FShaderFolderNode> ViewRoot = RootFolderData;
	if (TSharedPtr<FShaderFolderNode>* Found = FolderNodeMap.Find(GetCurrentPath()))
	{
		ViewRoot = *Found;
	}
	else
	{
		return;
	}

	// Build a fresh throwaway FTreeMapNodeData tree
	TSharedRef<FTreeMapNodeData> NewRoot = UE::ShaderAudit::BuildTreeMapView(ViewRoot.ToSharedRef(), MaxTreeDepth, Session->HasBytecodeDatabase());

	// Copy into persistent root (STreeMap holds a ref to it)
	RootTreeMapNode->Children = NewRoot->Children;
	RootTreeMapNode->Name = NewRoot->Name;
	RootTreeMapNode->Name2 = NewRoot->Name2;
	RootTreeMapNode->Size = NewRoot->Size;
	RootTreeMapNode->CenterText = NewRoot->CenterText;
	RootTreeMapNode->Color = NewRoot->Color;
	RootTreeMapNode->LogicalName = NewRoot->LogicalName;

	// Re-parent children to point to our persistent root
	for (const TSharedPtr<FTreeMapNodeData>& Child : RootTreeMapNode->Children)
	{
		Child->Parent = RootTreeMapNode.Get();
	}

	if (TreeMapWidget.IsValid())
	{
		TreeMapWidget->SetTreeRoot(RootTreeMapNode.ToSharedRef(), false);
		TreeMapWidget->RebuildTreeMap(false);
		// STreeMap doesn't self-invalidate; force the enclosing SInvalidationPanel to refresh.
		TreeMapWidget->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SShaderCostTreeMap::OnTreeMapNodeDoubleClicked(FTreeMapNodeData& NodeData, const FPointerEvent& MouseEvent)
{
	// LogicalName is either a folder FullPath or an asset AssetPath
	if (FolderNodeMap.Contains(NodeData.LogicalName))
	{
		TSharedPtr<FShaderFolderNode>& Node = FolderNodeMap[NodeData.LogicalName];
		NavigateTo(Node->FullPath);
	}
}

void SShaderCostTreeMap::OnTreeMapNodeRightClicked(FTreeMapNodeData& NodeData, const FPointerEvent& MouseEvent)
{
	if (!FolderNodeMap.Contains(NodeData.LogicalName))
	{
		return;
	}

	TSharedPtr<SWidget> ContextMenu = BuildAssetContextMenu(FolderNodeMap[NodeData.LogicalName]);
	if (!ContextMenu.IsValid())
	{
		return;
	}

	FWidgetPath WidgetPath;
	FSlateApplication::Get().GeneratePathToWidgetUnchecked(TreeMapWidget.ToSharedRef(), WidgetPath);
	FSlateApplication::Get().PushMenu(
		TreeMapWidget.ToSharedRef(),
		WidgetPath,
		ContextMenu.ToSharedRef(),
		MouseEvent.GetScreenSpacePosition(),
		FPopupTransitionEffect::ContextMenu
	);
}

void SShaderCostTreeMap::OnTreeMapWheelNavigate(const FString& TargetPath)
{
	NavigateTo(TargetPath);
}

// ============================================================================
// Mouse Back/Forward (ThumbMouseButton)
// ============================================================================

FReply SShaderCostTreeMap::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::ThumbMouseButton)
	{
		// Back: go to previous entry in history
		if (HistoryIndex > 0)
		{
			HistoryIndex--;
			bIsHistoryNavigation = true;
			NavigateTo(NavigationHistory[HistoryIndex]);
			bIsHistoryNavigation = false;
		}
		return FReply::Handled();
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::ThumbMouseButton2)
	{
		// Forward: go to next entry in history
		if (HistoryIndex < NavigationHistory.Num() - 1)
		{
			HistoryIndex++;
			bIsHistoryNavigation = true;
			NavigateTo(NavigationHistory[HistoryIndex]);
			bIsHistoryNavigation = false;
		}
		return FReply::Handled();
	}

	return SCompoundWidget::OnMouseButtonUp(MyGeometry, MouseEvent);
}

// ============================================================================
// Folder Tree
// ============================================================================

void SShaderCostTreeMap::RebuildFolderTree()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SShaderCostTreeMap::RebuildFolderTree);
	SidebarRootNodes.Empty();

	if (!RootFolderData.IsValid())
	{
		if (FolderTreeWidget.IsValid())
		{
			FolderTreeWidget->RequestTreeRefresh();
		}
		return;
	}

	// Root node is the top-level entry
	SidebarRootNodes.Add(RootFolderData);

	SidebarRootNodes.Sort([](const TSharedPtr<FShaderFolderNode>& A, const TSharedPtr<FShaderFolderNode>& B)
	{
		return A->FullPath < B->FullPath;
	});

	if (FolderTreeWidget.IsValid())
	{
		FolderTreeWidget->RequestTreeRefresh();
		// Keep root expanded so navigation sync always works
		FolderTreeWidget->SetItemExpansion(RootFolderData, true);
	}
}

TSharedRef<ITableRow> SShaderCostTreeMap::OnGenerateFolderRow(TSharedPtr<FShaderFolderNode> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	float DisplayCost = Item->Cost;

	return SNew(STableRow<TSharedPtr<FShaderFolderNode>>, OwnerTable)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2.f, 1.f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Item->Name))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.f, 1.f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("(%s)"), *UE::ShaderAudit::Utils::FormatCost(DisplayCost, Session.IsValid() && Session->BytecodeDatabase.IsValid()))))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Font(FCoreStyle::Get().GetFontStyle("SmallFont"))
		]
	];
}

void SShaderCostTreeMap::OnGetFolderChildren(TSharedPtr<FShaderFolderNode> Item, TArray<TSharedPtr<FShaderFolderNode>>& OutChildren)
{
	if (Item.IsValid())
	{
		// Only show folder children (not asset leaves) that have visible shaders under the current filter
		for (const TSharedPtr<FShaderFolderNode>& Child : Item->Children)
		{
			// When refcount filtering is active, skip folders with zero filtered cost
			if (Child->Cost <= 0.f)
			{
				continue;
			}
			OutChildren.Add(Child);
		}
		OutChildren.Sort([](const TSharedPtr<FShaderFolderNode>& A, const TSharedPtr<FShaderFolderNode>& B)
		{
			return A->Name < B->Name;
		});
	}
}

void SShaderCostTreeMap::OnFolderSelectionChanged(TSharedPtr<FShaderFolderNode> Item, ESelectInfo::Type SelectInfo)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SShaderCostTreeMap::OnFolderSelectionChanged);
	if (!Item.IsValid())
	{
		return;
	}

	NavigateTo(Item->FullPath);
}

// ============================================================================
// Detail Panel
// ============================================================================

void SShaderCostTreeMap::RefreshDetailPanel(const FString& CurrentPath)
{
	if (TSharedPtr<FShaderFolderNode>* Found = FolderNodeMap.Find(CurrentPath))
	{
		bool bIsFolderView = (*Found)->Children.Num() > 0;
		if (bIsFolderView)
		{
			ShowFolderDetail((*Found)->FullPath);
		}
		else
		{
			ShowAssetDetail((*Found)->MaterialIndex);
		}
	}
}

void SShaderCostTreeMap::ShowFolderDetail(const FString& FolderPath)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SShaderCostTreeMap::ShowFolderDetail);
	DetailRows.Empty();

	if (DetailSwitcher.IsValid())
	{
		DetailSwitcher->SetActiveWidgetIndex(0);
	}

	if (!Session.IsValid())
	{
		DetailTitle = LOCTEXT("NoData", "No data loaded");
		if (FolderDetailListWidget.IsValid())
		{
			FolderDetailListWidget->RequestListRefresh();
		}
		return;
	}

	DetailTitle = FText::Format(LOCTEXT("TopAssets", "Top assets in {0}"),
		FText::FromString(FolderPath));

	// Collect all leaf assets under this folder path from FShaderFolderNode tree
	TSharedPtr<FShaderFolderNode> FolderNode = RootFolderData;
	if (TSharedPtr<FShaderFolderNode>* Found = FolderNodeMap.Find(FolderPath))
	{
		FolderNode = *Found;
	}

	// Gather all asset descendants (Cost/ShaderCount already filtered in-place)
	TArray<TSharedPtr<FShaderFolderNode>> AssetNodes;
	TFunction<void(const TSharedPtr<FShaderFolderNode>&)> CollectAssets =
		[&CollectAssets, &AssetNodes](const TSharedPtr<FShaderFolderNode>& Node)
	{
		if ((Node->MaterialIndex != INDEX_NONE) && (Node->Cost > 0.f))
		{
			AssetNodes.Add(Node);
		}
		for (const auto& Child : Node->Children)
		{
			CollectAssets(Child);
		}
	};
	CollectAssets(FolderNode);

	// Sort by cost descending
	AssetNodes.Sort([](const TSharedPtr<FShaderFolderNode>& A, const TSharedPtr<FShaderFolderNode>& B)
	{
		return A->Cost > B->Cost;
	});

	// Take top 50
	const int32 MaxRows = FMath::Min(AssetNodes.Num(), 50);
	for (int32 i = 0; i < MaxRows; i++)
	{
		const TSharedPtr<FShaderFolderNode>& AssetNode = AssetNodes[i];

		TSharedPtr<FShaderDetailRow> Row = MakeShared<FShaderDetailRow>();
		Row->MaterialIndex = AssetNode->MaterialIndex;
		if (Session.IsValid() && Session->UniqueMaterials.IsValidIndex(AssetNode->MaterialIndex))
		{
			const FShaderAuditSession::FUniqueMaterial& Mat = Session->UniqueMaterials[AssetNode->MaterialIndex];
			Row->Label = FPaths::GetPathLeaf(Mat.Path);
			Row->ClassName = Mat.ClassName;
			Row->AssetPath = Mat.Path;
		}
		else
		{
			Row->Label = AssetNode->Name;
			Row->ClassName = AssetNode->ClassName;
		}
		Row->ShaderCount = AssetNode->ShaderCount;
		Row->Cost = AssetNode->Cost;
		Row->bSizeWeighted = Session->BytecodeDatabase.IsValid();

		DetailRows.Add(Row);
	}

	if (FolderDetailListWidget.IsValid())
	{
		FolderDetailListWidget->RequestListRefresh();
	}
}

TArray<TSharedPtr<FShaderDetailRow>> BuildMaterialDetailRows(
	const FShaderAuditSession& Session,
	int32 MaterialIndex,
	const TBitArray<>* VisibleShaders)
{
	TArray<TSharedPtr<FShaderDetailRow>> Rows;

	if (!Session.UniqueMaterials.IsValidIndex(MaterialIndex))
	{
		return Rows;
	}

	const FShaderAuditSession::FUniqueMaterial& Mat = Session.UniqueMaterials[MaterialIndex];

	// Filter to visible shaders
	TArray<int32> FilteredIndices;
	for (int32 Idx : Mat.ShaderIndices)
	{
		if (!VisibleShaders || (*VisibleShaders)[Idx])
		{
			FilteredIndices.Add(Idx);
		}
	}

	// Sort by shader size descending (largest first), fall back to refcount ascending
	FilteredIndices.Sort([&Session](int32 A, int32 B)
	{
		uint32 SizeA = 0, SizeB = 0;
		if (Session.BytecodeDatabase.IsValid())
		{
			if (const FShaderBytecodeInfo* InfoA = Session.BytecodeDatabase->Find(Session.StableShaderKeyAndValueArray[A].OutputHash))
			{
				SizeA = InfoA->CompressedSize;
			}
			if (const FShaderBytecodeInfo* InfoB = Session.BytecodeDatabase->Find(Session.StableShaderKeyAndValueArray[B].OutputHash))
			{
				SizeB = InfoB->CompressedSize;
			}
		}
		if (SizeA != SizeB)
		{
			return SizeA > SizeB;
		}
		const int32 RCA = Session.GetHashRefCount(Session.StableShaderKeyAndValueArray[A].OutputHash);
		const int32 RCB = Session.GetHashRefCount(Session.StableShaderKeyAndValueArray[B].OutputHash);
		return RCA < RCB;
	});

	for (int32 Idx : FilteredIndices)
	{
		const FStableShaderKeyAndValue& Entry = Session.StableShaderKeyAndValueArray[Idx];
		TSharedPtr<FShaderDetailRow> Row = MakeShared<FShaderDetailRow>();
		Row->ShaderType = Entry.ShaderType;
		Row->VFType = Entry.VFType;
		Row->PermutationId = Entry.PermutationId;
		Row->HashString = Entry.OutputHash.ToString();
		Row->RefCount = Session.GetHashRefCount(Entry.OutputHash);

		if (Session.BytecodeDatabase.IsValid())
		{
			uint8 Frequency = 0;
			uint32 CompressedSize = 0;
			uint32 UncompressedSize = 0;
			if (Session.GetShaderBytecodeInfo(Entry.OutputHash, Frequency, CompressedSize, UncompressedSize))
			{
				Row->Size = CompressedSize;
			}
			Row->bSizeWeighted = true;
		}

		Rows.Add(Row);
	}

	return Rows;
}

void SShaderCostTreeMap::ShowAssetDetail(int32 MaterialIndex)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SShaderCostTreeMap::ShowAssetDetail);
	DetailRows.Empty();

	if (DetailSwitcher.IsValid())
	{
		DetailSwitcher->SetActiveWidgetIndex(1);
	}

	if (!Session.IsValid() || !Session->UniqueMaterials.IsValidIndex(MaterialIndex))
	{
		return;
	}

	DetailRows = BuildMaterialDetailRows(*Session, MaterialIndex, &VisibleShaders);

	const FShaderAuditSession::FUniqueMaterial& Mat = Session->UniqueMaterials[MaterialIndex];
	DetailTitle = FText::Format(LOCTEXT("AssetDetail", "{0} ({1}) -- {2} shaders"),
		FText::FromString(Mat.Path), FText::FromString(Mat.ClassName), FText::AsNumber(DetailRows.Num()));

	if (ShaderDetailListWidget.IsValid())
	{
		ShaderDetailListWidget->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SShaderCostTreeMap::OnGenerateFolderDetailRow(TSharedPtr<FShaderDetailRow> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FShaderDetailRow>>, OwnerTable)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(0.4f)
		.VAlign(VAlign_Center)
		.Padding(4.f, 1.f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Item->Label))
			.ToolTipText(FText::FromString(Item->AssetPath))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(0.25f)
		.VAlign(VAlign_Center)
		.Padding(4.f, 1.f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Item->ClassName))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(0.15f)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Right)
		.Padding(4.f, 1.f)
		[
			SNew(STextBlock)
			.Text(FText::AsNumber(Item->ShaderCount))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(0.2f)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Right)
		.Padding(4.f, 1.f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(UE::ShaderAudit::Utils::FormatCost(Item->Cost, Item->bSizeWeighted)))
		]
	];
}

TSharedRef<ITableRow> SShaderCostTreeMap::OnGenerateShaderDetailRow(TSharedPtr<FShaderDetailRow> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SShaderDetailTableRow, OwnerTable)
		.Item(Item);
}

void SShaderCostTreeMap::OnDetailRowDoubleClicked(TSharedPtr<FShaderDetailRow> Item)
{
	if (!Item.IsValid() || Item->MaterialIndex == INDEX_NONE)
	{
		return;
	}

	// Navigate using the material index key
	NavigateTo(Item->AssetPath);
}

TSharedPtr<SWidget> SShaderCostTreeMap::BuildAssetContextMenu(const TSharedPtr<FShaderFolderNode>& Node)
{
	if (!Node.IsValid() || !Session.IsValid())
	{
		return TSharedPtr<SWidget>();
	}

	// Prefer EditorPath (set for generated sub-object nodes and their authored proxy folder)
	// over the raw SHK material path, which may be unloadable for generated sub-objects.
	const bool bHasEditorPath = !Node->EditorPath.IsEmpty();
	const bool bHasMaterial = Session->UniqueMaterials.IsValidIndex(Node->MaterialIndex);
	if (!bHasEditorPath && !bHasMaterial)
	{
		return TSharedPtr<SWidget>();
	}

	const FString AssetPath = bHasEditorPath
		? Node->EditorPath
		: Session->UniqueMaterials[Node->MaterialIndex].Path;

	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("BrowseToAsset", "Browse to Asset"),
		FText::FromString(AssetPath),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, AssetPath]()
		{
			OpenAssetInContentBrowser(AssetPath);
		}))
	);

	// Caller-supplied entries (e.g. editor adds "Find Similar Instances..." here).
	OnExtendAssetContextMenuHook.ExecuteIfBound(MenuBuilder, AssetPath);

	return MenuBuilder.MakeWidget();
}

void SShaderCostTreeMap::OpenAssetInContentBrowser(const FString& AssetPath)
{
	if (OnOpenAssetInContentBrowserHook.IsBound())
	{
		OnOpenAssetInContentBrowserHook.Execute(AssetPath);
	}
	else
	{
		UE_LOGF(LogShaderAudit, Log, "Browse to asset: %ls (not available in standalone mode)", *AssetPath);
	}
}

// ============================================================================
// Navigation
// ============================================================================

void SShaderCostTreeMap::NavigateTo(const FString& Path)
{
	if (bIsNavigating) { return; }
	TGuardValue<bool> NavigationGuard(bIsNavigating, true);
	TRACE_CPUPROFILER_EVENT_SCOPE(SShaderCostTreeMap::NavigateTo);

	// Resolve ".." -- go up one level from current path
	FString ResolvedPath;
	if (Path == TEXT(".."))
	{
		FStringView Current(GetCurrentPath());
		if (Current == TEXTVIEW("/"))
		{
			return; // Already at root
		}
		// Look up current node's parent
		if (TSharedPtr<FShaderFolderNode>* CurrentNode = FolderNodeMap.Find(GetCurrentPath()))
		{
			if (TSharedPtr<FShaderFolderNode> Parent = (*CurrentNode)->Parent.Pin())
			{
				ResolvedPath = Parent->FullPath;
			}
			else
			{
				ResolvedPath = TEXT("/");
			}
		}
		else
		{
			return;
		}
	}
	else
	{
		ResolvedPath = Path;
	}

	// Look up in FolderNodeMap -- single authority for all paths
	TSharedPtr<FShaderFolderNode>* Found = FolderNodeMap.Find(ResolvedPath);
	if (!Found)
	{
		return;
	}

	// Update current path
	if (!bIsHistoryNavigation)
	{
		// Truncate forward history and push new entry
		NavigationHistory.SetNum(HistoryIndex + 1);
		NavigationHistory.Add(ResolvedPath);
		HistoryIndex = NavigationHistory.Num() - 1;
	}

	// Determine the folder to use as treemap root
	FString FolderPath = (*Found)->FullPath;

	RebuildTreeMap();
	RebuildBreadcrumb();
	ExpandFolderTreeTo(FolderPath);
	RefreshDetailPanel(FolderPath);
}

void SShaderCostTreeMap::ExpandFolderTreeTo(const FString& FolderPath)
{
	if (!FolderTreeWidget.IsValid())
	{
		return;
	}

	// Walk parent chain to expand all ancestors
	TSharedPtr<FShaderFolderNode>* TargetPtr = FolderNodeMap.Find(FolderPath);
	if (!TargetPtr)
	{
		return;
	}

	// Collect ancestors
	TArray<TSharedPtr<FShaderFolderNode>> Ancestors;
	TSharedPtr<FShaderFolderNode> Walk = (*TargetPtr)->Parent.Pin();
	while (Walk.IsValid())
	{
		Ancestors.Add(Walk);
		Walk = Walk->Parent.Pin();
	}

	// Expand root-to-leaf
	for (int32 i = Ancestors.Num() - 1; i >= 0; --i)
	{
		FolderTreeWidget->SetItemExpansion(Ancestors[i], true);
	}

	// Select and scroll to target
	FolderTreeWidget->SetSelection(*TargetPtr, ESelectInfo::Direct);
	FolderTreeWidget->RequestScrollIntoView(*TargetPtr);
}

// ============================================================================
// Breadcrumb
// ============================================================================

void SShaderCostTreeMap::RebuildBreadcrumb()
{
	if (!BreadcrumbTrail.IsValid())
	{
		return;
	}

	BreadcrumbTrail->ClearCrumbs();

	// Walk the parent chain from current node to build breadcrumbs.
	// This works for both folder paths and hierarchy paths.
	TSharedPtr<FShaderFolderNode>* CurrentNode = FolderNodeMap.Find(GetCurrentPath());
	if (CurrentNode && CurrentNode->IsValid())
	{
		// Collect ancestors (excluding root which is already pushed)
		TArray<TSharedPtr<FShaderFolderNode>> Ancestors;
		TSharedPtr<FShaderFolderNode> Walk = *CurrentNode;
		while (Walk.IsValid() && Walk->FullPath != TEXT("/"))
		{
			Ancestors.Add(Walk);
			Walk = Walk->Parent.Pin();
		}

		// Push in root-to-leaf order
		for (int32 i = Ancestors.Num() - 1; i >= 0; --i)
		{
			BreadcrumbTrail->PushCrumb(FText::FromString(Ancestors[i]->Name), Ancestors[i]->FullPath);
		}
	}
}

void SShaderCostTreeMap::OnBreadcrumbClicked(const FString& Path)
{
	NavigateTo(Path);
}

void SShaderCostTreeMap::StartPathEdit()
{
	if (PathSwitcher.IsValid() && PathEditBox.IsValid())
	{
		PathEditBox->SetText(FText::FromString(GetCurrentPath()));
		PathSwitcher->SetActiveWidgetIndex(1);
		FSlateApplication::Get().SetKeyboardFocus(PathEditBox);
	}
}

void SShaderCostTreeMap::OnPathEditCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	// Switch back to breadcrumb view
	if (PathSwitcher.IsValid())
	{
		PathSwitcher->SetActiveWidgetIndex(0);
	}

	if (CommitType != ETextCommit::OnEnter)
	{
		return;
	}

	FString Path = NewText.ToString().TrimStartAndEnd();
	if (Path.IsEmpty())
	{
		Path = TEXT("/");
	}

	NavigateTo(Path);
}

// ============================================================================
// Refcount Slider
// ============================================================================

void SShaderCostTreeMap::OnRefCountCommitted(int32 NewValue, ETextCommit::Type CommitType)
{
	MaxRefCount = FMath::Max(0, NewValue); // 0 = no filter
	ApplyFilters();
}

void SShaderCostTreeMap::OnFilterCommitted(const FText& Text, ETextCommit::Type CommitType)
{
	if (CommitType != ETextCommit::OnEnter || !Session.IsValid())
	{
		return;
	}

	const FString Expression = Text.ToString().TrimStartAndEnd();
	if (Expression.IsEmpty())
	{
		return;
	}

	FShaderFilterNode Root;
	FString Error;
	if (!FShaderFilterNode::Parse(Expression, Root, Error))
	{
		UE_LOGF(LogShaderAudit, Warning, "Filter parse error: %ls", *Error);
		// TODO: show error in UI
		return;
	}

	ActiveFilters.Add(MoveTemp(Root));
	ActiveFilterStrings.Add(Expression);
	RebuildFilterTags();
	ApplyFilters();
}

void SShaderCostTreeMap::RemoveFilter(int32 Index)
{
	if (ActiveFilters.IsValidIndex(Index))
	{
		ActiveFilters.RemoveAt(Index);
		ActiveFilterStrings.RemoveAt(Index);
		RebuildFilterTags();
		ApplyFilters();
	}
}

void SShaderCostTreeMap::ApplyFilters()
{
	if (!Session.IsValid())
	{
		return;
	}

	BuildVisibleShaders(*Session, ActiveFilters, MaxRefCount, VisibleShaders);
	RebuildFromFilters();
}

void SShaderCostTreeMap::RebuildFilterTags()
{
	if (!FilterTagBox.IsValid())
	{
		return;
	}

	FilterTagBox->ClearChildren();

	for (int32 Idx = 0; Idx < ActiveFilterStrings.Num(); ++Idx)
	{
		const int32 FilterIndex = Idx;

		FilterTagBox->AddSlot()
		.Padding(2.f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(6.f, 2.f))
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(ActiveFilterStrings[Idx]))
					.Font(FCoreStyle::Get().GetFontStyle("SmallFont"))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.f, 0.f, 0.f, 0.f)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FCoreStyle::Get(), "NoBorder")
					.ContentPadding(0)
					.OnClicked_Lambda([this, FilterIndex]() -> FReply
					{
						RemoveFilter(FilterIndex);
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("x")))
						.Font(FCoreStyle::Get().GetFontStyle("SmallFont"))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
				]
			]
		];
	}
}

void SShaderCostTreeMap::GetFilterSuggestions(const FString& Text, TArray<FString>& OutSuggestions)
{
	GetShaderFilterSuggestions(Text, Session.Get(), OutSuggestions);
}

FText SShaderCostTreeMap::GetRefCountLabel() const
{
	if (MaxRefCount <= 0)
	{
		return LOCTEXT("AllRefCounts", "All");
	}
	return FText::Format(LOCTEXT("MaxRC", "<={0}"), FText::AsNumber(MaxRefCount));
}

FText SShaderCostTreeMap::GetStatsLabel() const
{
	return CachedStatsLabel;
}

void SShaderCostTreeMap::UpdateCachedStats()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SShaderCostTreeMap::UpdateCachedStats);
	if (!Session.IsValid())
	{
		CachedStatsLabel = LOCTEXT("NoDataStats", "No data");
		return;
	}

	// Read directly from the filtered folder tree
	int32 TotalShaders = RootFolderData.IsValid() ? RootFolderData->ShaderCount : 0;
	int32 TotalAssets = 0;
	int32 UniqueHashes = 0;

	// Count assets from the folder node map
	for (const auto& Pair : FolderNodeMap)
	{
		if (Pair.Value->bIsAsset && Pair.Value->ShaderCount > 0)
		{
			TotalAssets++;
		}
	}

	// Count unique visible hashes
	TSet<FShaderHash> VisibleHashes;
	for (int32 Idx = 0; Idx < VisibleShaders.Num(); ++Idx)
	{
		if (VisibleShaders[Idx])
		{
			VisibleHashes.Add(Session->StableShaderKeyAndValueArray[Idx].OutputHash);
		}
	}
	UniqueHashes = VisibleHashes.Num();

	CachedStatsLabel = FText::Format(LOCTEXT("Stats", "Shaders: {0} | Assets: {1} | Unique hashes: {2}"),
		FText::AsNumber(TotalShaders),
		FText::AsNumber(TotalAssets),
		FText::AsNumber(UniqueHashes));
}

void SShaderCostTreeMap::OnDepthCommitted(int32 NewValue, ETextCommit::Type CommitType)
{
	MaxTreeDepth = FMath::Max(1, NewValue);

	RebuildTreeMap();

	RefreshDetailPanel(GetCurrentPath());
}

FText SShaderCostTreeMap::GetDepthLabel() const
{
	if (MaxTreeDepth <= 0)
	{
		return LOCTEXT("DepthAll", "All");
	}
	return FText::AsNumber(MaxTreeDepth);
}

#undef LOCTEXT_NAMESPACE
