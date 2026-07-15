// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SShaderCostTreeMap.h"
#include "ShaderAuditSession.h"
#include "ITreeMap.h"
#include "STreeMap.h"

#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSuggestionTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SInvalidationPanel.h"
#include "Styling/CoreStyle.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "ShaderCostTreeMap"

namespace UE::ShaderAudit::TreeMapCVars
{
	// Cells whose predicted screen width is below this drop their labels entirely.
	// 0 disables the cull (all cells get labels regardless of size).
	static float MinLabelWidthPx = 40.f;
	static FAutoConsoleVariableRef CVarMinLabelWidthPx(
		TEXT("ShaderAudit.TreeMap.MinLabelWidthPx"),
		MinLabelWidthPx,
		TEXT("Minimum predicted cell width (in pixels) below which TreeMap cell labels are hidden. 0 disables. Default: 80."),
		ECVF_Default);

	// Approximate average character width for the body font. Used to truncate labels
	// to what will visually fit so the font shaper doesn't waste work on clipped chars.
	static float ApproxCharWidthPx = 6.5f;
	static FAutoConsoleVariableRef CVarApproxCharWidthPx(
		TEXT("ShaderAudit.TreeMap.ApproxCharWidthPx"),
		ApproxCharWidthPx,
		TEXT("Approximate average character width (in pixels) used to pre-truncate TreeMap labels. Lower values keep more characters. Default: 6.5."),
		ECVF_Default);

	// Padding reserved around text inside a cell.
	static float CellTextPaddingPx = 4.f;
	static FAutoConsoleVariableRef CVarCellTextPaddingPx(
		TEXT("ShaderAudit.TreeMap.CellTextPaddingPx"),
		CellTextPaddingPx,
		TEXT("Pixels reserved around TreeMap cell text when computing truncation. Default: 4."),
		ECVF_Default);
}


// ============================================================================
// STreeMap subclass for mouse-wheel navigation
// ============================================================================

class SShaderCostSTreeMap : public STreeMap
{
public:
	DECLARE_DELEGATE_OneParam(FOnWheelNavigate, const FString&);

	SLATE_BEGIN_ARGS(SShaderCostSTreeMap)
		: _AllowEditing(false)
	{}
		SLATE_ATTRIBUTE(bool, AllowEditing)
		SLATE_EVENT(FOnTreeMapNodeInteracted, OnTreeMapNodeDoubleClicked)
		SLATE_EVENT(FOnTreeMapNodeInteracted, OnTreeMapNodeRightClicked)
		SLATE_EVENT(FOnWheelNavigate, OnWheelNavigate)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FTreeMapNodeData>& InTreeMapNodeData, const TSharedPtr<ITreeMapCustomization>& InCustomization)
	{
		OnWheelNavigate = InArgs._OnWheelNavigate;

		STreeMap::FArguments ParentArgs;
		ParentArgs._AllowEditing = InArgs._AllowEditing;
		ParentArgs._OnTreeMapNodeDoubleClicked = InArgs._OnTreeMapNodeDoubleClicked;
		ParentArgs._OnTreeMapNodeRightClicked = InArgs._OnTreeMapNodeRightClicked;

		STreeMap::Construct(ParentArgs, InTreeMapNodeData, InCustomization);
	}

	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (IsNavigationTransitionActive() || !OnWheelNavigate.IsBound())
		{
			return FReply::Unhandled();
		}

		if (MouseEvent.GetWheelDelta() > 0)
		{
			const FTreeMapNodeVisualInfo* NodeVisualUnderCursor = FindNodeVisualUnderCursor(MyGeometry, MouseEvent.GetScreenSpacePosition());
			if (NodeVisualUnderCursor != nullptr)
			{
				FTreeMapNodeDataPtr CurrentRoot = GetTreeRoot();
				FTreeMapNodeDataPtr NextNode = NodeVisualUnderCursor->NodeData->AsShared();
				do
				{
					FTreeMapNodeDataPtr NodeParent;
					if (NextNode->Parent != nullptr)
					{
						NodeParent = NextNode->Parent->AsShared();
					}

					if (NodeParent == CurrentRoot)
					{
						break;
					}
					NextNode = NodeParent;
				}
				while (NextNode.IsValid());

				if (NextNode.IsValid() && !NextNode->LogicalName.IsEmpty())
				{
					OnWheelNavigate.Execute(NextNode->LogicalName);
					return FReply::Handled();
				}
			}
		}
		else if (MouseEvent.GetWheelDelta() < 0)
		{
			FTreeMapNodeDataPtr CurrentRoot = GetTreeRoot();
			if (CurrentRoot.IsValid())
			{
				OnWheelNavigate.Execute(TEXT(".."));
				return FReply::Handled();
			}
		}

		return FReply::Unhandled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::ThumbMouseButton ||
			MouseEvent.GetEffectingButton() == EKeys::ThumbMouseButton2)
		{
			return FReply::Unhandled();
		}

		return STreeMap::OnMouseButtonUp(MyGeometry, MouseEvent);
	}
	// STreeMap mutates MouseOverVisual on mouse-move/leave but never calls Invalidate(),
	// so an enclosing SInvalidationPanel keeps showing stale hover state. Force a paint
	// invalidation so the cached frame is rebuilt when the hover target changes.
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		FReply Reply = STreeMap::OnMouseMove(MyGeometry, MouseEvent);
		Invalidate(EInvalidateWidgetReason::Paint);
		return Reply;
	}

	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override
	{
		STreeMap::OnMouseLeave(MouseEvent);
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	// Predict each cell's screen size from its cost ratio and clear Name/Name2/CenterText
	// on cells too small to show readable text. STreeMap::OnPaint already gates text emission
	// on !Name.IsEmpty(), so this saves ~6 FSlateDrawElements per skipped cell (3 strings *
	// shadow+foreground passes) without touching engine code.
	//
	// Names are restored automatically: BuildTreeMapView() rebuilds the FTreeMapNodeData tree
	// from FShaderFolderNode on every filter/navigation/mode change, then Tick re-trims with
	// the new geometry. Self-correcting on zoom-in (a small cell becomes the new root and gets
	// its label back).
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		STreeMap::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

		const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
		const FTreeMapNodeDataPtr Root = GetTreeRoot();
		if (!Root.IsValid() || LocalSize.X <= 0.f || LocalSize.Y <= 0.f)
		{
			return;
		}

		// Trim every tick. Walking the tree is O(N) with simple arithmetic per node
		// (a few microseconds for the cell counts we deal with), and a stale cache costs
		// far more than re-walking: trimming is destructive (Name fields are mutated in
		// place), but BuildTreeMapView regenerates names on every filter/navigation/mode
		// change, so this stays in sync.
		const float TotalArea = LocalSize.X * LocalSize.Y;
		TrimSmallCellLabels(*Root, TotalArea, Root->Size);
	}

private:
	// Predict each cell's screen size from its cost ratio (cell area = (node.Size / parent.Size) * parent area).
	// Cells are treated as roughly square (sqrt of area) since the squarified treemap algorithm targets
	// aspect ratios near 1. Sliver cells may be misclassified; the only visible artifact is a label
	// appearing/disappearing one frame "early" - acceptable trade-off for the perf win.
	//
	// Tuning knobs are CVars defined at the top of this file:
	//   ShaderAudit.TreeMap.MinLabelWidthPx     - cells narrower than this drop labels entirely (0 = disable)
	//   ShaderAudit.TreeMap.ApproxCharWidthPx   - char width estimate for label truncation
	//   ShaderAudit.TreeMap.CellTextPaddingPx   - padding reserved around cell text

	static void TruncateToFit(FString& InOut, float CellWidthPx)
	{
		if (InOut.IsEmpty())
		{
			return;
		}
		const float CharWidth = FMath::Max(0.5f, UE::ShaderAudit::TreeMapCVars::ApproxCharWidthPx);
		const float UsableWidth = FMath::Max(0.f, CellWidthPx - UE::ShaderAudit::TreeMapCVars::CellTextPaddingPx);
		const int32 MaxChars = FMath::FloorToInt(UsableWidth / CharWidth);
		if (MaxChars <= 0)
		{
			InOut.Empty();
		}
		else if (InOut.Len() > MaxChars)
		{
			InOut.LeftInline(MaxChars, EAllowShrinking::No);
		}
	}


	static void TrimSmallCellLabels(FTreeMapNodeData& Node, float ParentArea, float ParentSize)
	{
		float CellArea = ParentArea;
		if (ParentSize > 0.f && Node.Size > 0.f)
		{
			CellArea = (Node.Size / ParentSize) * ParentArea;
			const float ApproxWidth = FMath::Sqrt(CellArea);
			const float MinLabelWidthPx = UE::ShaderAudit::TreeMapCVars::MinLabelWidthPx;
			if (MinLabelWidthPx > 0.f && ApproxWidth < MinLabelWidthPx)
			{
				Node.Name.Empty();
				Node.Name2.Empty();
				Node.CenterText.Empty();
			}
			else
			{
				// Cell is wide enough to keep a label, but pre-truncate to what will visibly fit.
				// Saves font shaper work for long labels that STreeMap would clip away 
				// to ~4 visible characters anyway.
				TruncateToFit(Node.Name, ApproxWidth);
				TruncateToFit(Node.Name2, ApproxWidth);
				TruncateToFit(Node.CenterText, ApproxWidth);
			}
		}

		for (const TSharedPtr<FTreeMapNodeData>& Child : Node.Children)
		{
			if (Child.IsValid())
			{
				TrimSmallCellLabels(*Child, CellArea, Node.Size);
			}
		}
	}

	FOnWheelNavigate OnWheelNavigate;
};

// ============================================================================
// Construct
// ============================================================================

void SShaderCostTreeMap::Construct(const FArguments& InArgs)
{
	OnExtendAssetContextMenuHook = InArgs._OnExtendAssetContextMenu;
	OnOpenAssetInContentBrowserHook = InArgs._OnOpenAssetInContentBrowser;

	RootTreeMapNode = MakeShared<FTreeMapNodeData>();
	RootTreeMapNode->Name = TEXT("/");

	FolderDetailHeaderRow = SNew(SHeaderRow)
		+ SHeaderRow::Column("Name").DefaultLabel(LOCTEXT("FDName", "Name")).FillWidth(0.4f)
		+ SHeaderRow::Column("Class").DefaultLabel(LOCTEXT("FDClass", "Class")).FillWidth(0.25f)
		+ SHeaderRow::Column("Shaders").DefaultLabel(LOCTEXT("FDShaders", "Shaders")).FillWidth(0.15f).HAlignCell(HAlign_Right)
		+ SHeaderRow::Column("Cost").DefaultLabel(LOCTEXT("FDCost", "Cost")).FillWidth(0.2f).HAlignCell(HAlign_Right);

	ShaderDetailHeaderRow = SNew(SHeaderRow)
		+ SHeaderRow::Column("ShaderType").DefaultLabel(LOCTEXT("SDShaderType", "ShaderType")).FillWidth(0.2f)
		+ SHeaderRow::Column("VFType").DefaultLabel(LOCTEXT("SDVFType", "VFType")).FillWidth(0.15f)
		+ SHeaderRow::Column("Perm").DefaultLabel(LOCTEXT("SDPerm", "Perm")).FillWidth(0.15f)
		+ SHeaderRow::Column("Hash").DefaultLabel(LOCTEXT("SDHash", "Hash")).FillWidth(0.25f)
		+ SHeaderRow::Column("RefCount").DefaultLabel(LOCTEXT("SDRefCount", "RefCount")).FillWidth(0.1f).HAlignCell(HAlign_Right)
		+ SHeaderRow::Column("Size").DefaultLabel(LOCTEXT("SDSize", "Size")).FillWidth(0.15f).HAlignCell(HAlign_Right);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4.f)               [ MakeToolbar() ]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 2.f, 4.f, 0.f) [ MakeFilterBar() ]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 2.f, 4.f, 2.f) [ MakeFilterTagBar() ]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 2.f, 4.f, 2.f) [ MakePathBar() ]
		+ SVerticalBox::Slot().FillHeight(1.f)                          [ MakeContentArea() ]
	];
}

TSharedRef<SWidget> SShaderCostTreeMap::MakeToolbar()
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.f, 0.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MaxRefCount", "Max Refcount:"))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.f, 0.f)
		[
			SNew(SNumericEntryBox<int32>)
			.AllowSpin(true)
			.MinValue(0)
			.MaxValue(100)
			.Value(this, &SShaderCostTreeMap::GetRefCountValue)
			.OnValueCommitted(this, &SShaderCostTreeMap::OnRefCountCommitted)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.f, 0.f)
		[
			SNew(STextBlock)
			.Text(this, &SShaderCostTreeMap::GetRefCountLabel)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(16.f, 0.f, 4.f, 0.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TreeDepth", "Tree Depth:"))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.f, 0.f)
		[
			SNew(SNumericEntryBox<int32>)
			.AllowSpin(true)
			.MinValue(1)
			.MaxValue(20)
			.Value(this, &SShaderCostTreeMap::GetDepthValue)
			.OnValueCommitted(this, &SShaderCostTreeMap::OnDepthCommitted)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(16.f, 0.f, 4.f, 0.f)
		[
			SNew(SCheckBox)
			.Style(FCoreStyle::Get(), "ToggleButtonCheckbox")
			.IsChecked_Lambda([this]() { return TreeMapMode == ETreeMapMode::MaterialHierarchy ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
			{
				TreeMapMode = (NewState == ECheckBoxState::Checked) ? ETreeMapMode::MaterialHierarchy : ETreeMapMode::FolderTree;
				RebuildFromFilters();
				NavigateTo(TEXT("/"));
			})
			.ToolTipText(LOCTEXT("HierarchyToggleTip", "Toggle between folder view and material hierarchy view"))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("HierarchyToggle", "Material Hierarchy"))
			]
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Right)
		.Padding(8.f, 0.f)
		[
			SNew(STextBlock)
			.Text(this, &SShaderCostTreeMap::GetStatsLabel)
		];
}

TSharedRef<SWidget> SShaderCostTreeMap::MakeFilterBar()
{
	return SNew(SHorizontalBox)

		// Hash search (convenience shortcut)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SBox)
			.WidthOverride(220.f)
			[
				SAssignNew(HashSearchBox, SSearchBox)
				.HintText(LOCTEXT("HashSearchHint", "Hash contains..."))
				.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type CommitType)
				{
					if (CommitType != ETextCommit::OnEnter) { return; }
					const FString Hash = Text.ToString().TrimStartAndEnd();
					if (Hash.IsEmpty()) { return; }
					const FText Expression = FText::FromString(FString::Printf(TEXT("shader.hash contains %s"), *Hash));
					OnFilterCommitted(Expression, ETextCommit::OnEnter);
					HashSearchBox->SetText(FText::GetEmpty());
				})
			]
		]

		// Full expression filter
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		[
			SNew(SSuggestionTextBox)
			.HintText(LOCTEXT("FilterHint", "Filter: asset.class == Material && shader.size < 1MB"))
			.OnTextCommitted(this, &SShaderCostTreeMap::OnFilterCommitted)
			.OnShowingSuggestions_Lambda([this](const FString& Text, TArray<FString>& OutSuggestions)
			{
				GetFilterSuggestions(Text, OutSuggestions);
			})
			.ClearKeyboardFocusOnCommit(false)
		];
}

TSharedRef<SWidget> SShaderCostTreeMap::MakeFilterTagBar()
{
	return SAssignNew(FilterTagBox, SWrapBox)
		.UseAllottedSize(true);
}

TSharedRef<SWidget> SShaderCostTreeMap::MakePathBar()
{
	return SAssignNew(PathSwitcher, SWidgetSwitcher)
		.WidgetIndex(0)

		// Slot 0: Breadcrumb (default)
		+ SWidgetSwitcher::Slot()
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
			.OnMouseDoubleClick_Lambda([this](const FGeometry&, const FPointerEvent&) -> FReply
			{
				StartPathEdit();
				return FReply::Handled();
			})
			[
				SAssignNew(BreadcrumbTrail, SBreadcrumbTrail<FString>)
				.OnCrumbClicked(this, &SShaderCostTreeMap::OnBreadcrumbClicked)
			]
		]

		// Slot 1: Editable text box
		+ SWidgetSwitcher::Slot()
		[
			SAssignNew(PathEditBox, SEditableTextBox)
			.HintText(LOCTEXT("PathHint", "Type a path..."))
			.OnTextCommitted(this, &SShaderCostTreeMap::OnPathEditCommitted)
		];
}

TSharedRef<SWidget> SShaderCostTreeMap::MakeContentArea()
{
	return SNew(SSplitter)
		.Orientation(Orient_Horizontal)

		// Left: Folder tree
		+ SSplitter::Slot()
		.Value(0.25f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(2.f)
			[
				SNew(SInvalidationPanel)
				[
					SAssignNew(FolderTreeWidget, STreeView<TSharedPtr<FShaderFolderNode>>)
					.TreeItemsSource(&SidebarRootNodes)
					.OnGenerateRow(this, &SShaderCostTreeMap::OnGenerateFolderRow)
					.OnGetChildren(this, &SShaderCostTreeMap::OnGetFolderChildren)
					.OnSelectionChanged(this, &SShaderCostTreeMap::OnFolderSelectionChanged)
					.OnContextMenuOpening_Lambda([this]() -> TSharedPtr<SWidget>
					{
						if (!FolderTreeWidget.IsValid()) { return TSharedPtr<SWidget>(); }
						TArray<TSharedPtr<FShaderFolderNode>> Selected = FolderTreeWidget->GetSelectedItems();
						if (Selected.Num() == 0 || !Selected[0].IsValid()) { return TSharedPtr<SWidget>(); }
						return BuildAssetContextMenu(Selected[0]);
					})
					.SelectionMode(ESelectionMode::Single)
				]
			]
		]

		// Right: Detail panel + Treemap
		+ SSplitter::Slot()
		.Value(0.75f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Vertical)

			// Top-right: Detail panel
			+ SSplitter::Slot()
			.Value(0.35f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				.Padding(4.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 0.f, 0.f, 4.f)
					[
						SNew(STextBlock)
						.Text(this, &SShaderCostTreeMap::GetDetailTitle)
						.Font(FCoreStyle::Get().GetFontStyle("DetailsView.CategoryFontStyle"))
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.f)
					[
						SAssignNew(DetailSwitcher, SWidgetSwitcher)
						.WidgetIndex(0)

							// Slot 0: Folder detail (plain STextBlock rows)
						+ SWidgetSwitcher::Slot()
						[
							SNew(SInvalidationPanel)
							[
								SAssignNew(FolderDetailListWidget, SListView<TSharedPtr<FShaderDetailRow>>)
								.ListItemsSource(&DetailRows)
								.OnGenerateRow(this, &SShaderCostTreeMap::OnGenerateFolderDetailRow)
								.OnMouseButtonDoubleClick(this, &SShaderCostTreeMap::OnDetailRowDoubleClicked)
								.HeaderRow(FolderDetailHeaderRow.ToSharedRef())
							]
						]

							// Slot 1: Shader detail (selectable SEditableText rows)
						+ SWidgetSwitcher::Slot()
						[
							SNew(SInvalidationPanel)
							[
								SAssignNew(ShaderDetailListWidget, SListView<TSharedPtr<FShaderDetailRow>>)
								.ListItemsSource(&DetailRows)
								.OnGenerateRow(this, &SShaderCostTreeMap::OnGenerateShaderDetailRow)
								.HeaderRow(ShaderDetailHeaderRow.ToSharedRef())
							]
						]
					]
				]
			]
			// Bottom-right: Treemap
			+ SSplitter::Slot()
			.Value(0.65f)
			[
				SNew(SInvalidationPanel)
				[
					SAssignNew(TreeMapWidget, SShaderCostSTreeMap, RootTreeMapNode.ToSharedRef(), nullptr)
					.OnTreeMapNodeDoubleClicked(this, &SShaderCostTreeMap::OnTreeMapNodeDoubleClicked)
					.OnTreeMapNodeRightClicked(this, &SShaderCostTreeMap::OnTreeMapNodeRightClicked)
					.OnWheelNavigate(this, &SShaderCostTreeMap::OnTreeMapWheelNavigate)
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
