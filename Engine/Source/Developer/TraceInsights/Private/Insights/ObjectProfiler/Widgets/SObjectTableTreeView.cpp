// Copyright Epic Games, Inc. All Rights Reserved.

#include "SObjectTableTreeView.h"

#include "DesktopPlatformModule.h"
#include "Features/IModularFeatures.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "SlateOptMacros.h"
#include "Styling/StarshipCoreStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSegmentedControl.h"

// TraceServices
#include "Common/ProviderLock.h"
#include "TraceServices/Model/ObjectProvider.h"

// TraceInsightsCore
#include "InsightsCore/Common/Stopwatch.h"
#include "InsightsCore/Common/TimeUtils.h"
#include "InsightsCore/Table/ViewModels/Table.h"
#include "InsightsCore/Widgets/SSegmentedBarGraph.h"

// TraceInsights
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/ObjectProfiler/IAssetInfoProvider.h"
#include "Insights/ObjectProfiler/IObjectProfilerExtender.h"
#include "Insights/ObjectProfiler/ObjectProfilerManager.h"
#include "Insights/ObjectProfiler/ViewModels/ObjectGroupingByClassCategory.h"
#include "Insights/ObjectProfiler/ViewModels/ObjectGroupingByObjectName.h"
#include "Insights/ObjectProfiler/ViewModels/ObjectGroupingByOuter.h"
#include "Insights/ObjectProfiler/ViewModels/ObjectNode.h"
#include "Insights/ObjectProfiler/ViewModels/ObjectTable.h"
#include "Insights/ObjectProfiler/ViewModels/ObjectTableViewPresets.h"
#include "Insights/ObjectProfiler/Widgets/SObjectDetailsView.h"

#define INSIGHTS_SHOW_FOOTER 0

#define LOCTEXT_NAMESPACE "UE::Insights::ObjectProfiler::SObjectTableTreeView"

static bool GMaskAssetNames = false;
static FAutoConsoleVariableRef CVarMaskAssetNames(
	TEXT("Insights.MaskAssetNames"),
	GMaskAssetNames,
	TEXT("If enabled, internal asset names and paths are masked in the Object Insights tree view (UEFN simplified mode only)."),
	ECVF_Default
);

static bool GSkipScriptPackageActorAttribution = true;
static FAutoConsoleVariableRef CVarSkipScriptPackageActorAttribution(
	TEXT("Insights.MemorySnapshot.SkipScriptPackageActorAttribution"),
	GSkipScriptPackageActorAttribution,
	TEXT("If enabled, script packages (/Script/*) are excluded from actor attribution.")
	TEXT(" Script packages contain native C++ reflection metadata (UClass, UScriptStruct, UEnum)")
	TEXT(" which is matched at the package level, losing the individual type information.")
	TEXT(" Most actors reference these packages, producing expensive and uninformative reference chain searches."),
	ECVF_Default
);

static bool GSkipTransientPackageActorAttribution = true;
static FAutoConsoleVariableRef CVarSkipTransientPackageActorAttribution(
	TEXT("Insights.MemorySnapshot.SkipTransientPackageActorAttribution"),
	GSkipTransientPackageActorAttribution,
	TEXT("If enabled, engine transient packages (/Engine/Transient) are excluded from actor attribution.")
	TEXT(" The transient package contains a large number of temporary objects in memory,")
	TEXT(" few of which will match between the profiled target and the local target,")
	TEXT(" producing massive and unreliable reference chain searches."),
	ECVF_Default
);

static bool GSkipVersePackageActorAttribution = true;
static FAutoConsoleVariableRef CVarSkipVersePackageActorAttribution(
	TEXT("Insights.MemorySnapshot.SkipVersePackageActorAttribution"),
	GSkipVersePackageActorAttribution,
	TEXT("If enabled, Verse packages (/_Verse/*) are excluded from actor attribution.")
	TEXT(" Verse packages are generally referenced from entities rather than actors,")
	TEXT(" making actor attribution ineffective and expensive for these packages."),
	ECVF_Default
);

static bool ShouldSkipActorAttribution(const FAssetData& Data)
{
	if (Data.PackageName.IsNone())
	{
		return false;
	}

	FNameBuilder PackageNameView(Data.PackageName);

	if (GSkipScriptPackageActorAttribution && FPackageName::IsScriptPackage(PackageNameView))
	{
		return true;
	}
	if (GSkipTransientPackageActorAttribution && FPackageName::IsInEngineTransientPackages(PackageNameView))
	{
		return true;
	}
	if (GSkipVersePackageActorAttribution && FPackageName::IsVersePackage(PackageNameView))
	{
		return true;
	}

	return false;
}

namespace UE::Insights::ObjectProfiler
{

////////////////////////////////////////////////////////////////////////////////////////////////////

SObjectTableTreeView::SObjectTableTreeView()
{
	bRunInAsyncMode = true;

	MaxNodesToAutoExpand = 8;
	MaxDepthToAutoExpand = 1;
	MaxNodesToExpand = 1000000;
	MaxDepthToExpand = 100;

#if WITH_EDITOR
	bHideObjectsWithZeroReferencingActors = true;
	bCanShowReferencingActors = true;
#else
	bShowAdvancedUI = true;
	bIsViewPresetsDropDownVisible = true;
	bIsAdvancedFilterConfiguratorVisible = true;
	bIsHierarchyBreadcrumbTrailVisible = true;
#endif

	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Insights.EnableUEFNMode")))
	{
		bIsSimplifiedMode = CVar->GetBool();
	}
	if (bIsSimplifiedMode)
	{
		bShowAdvancedUI = false;
		bIsViewPresetsDropDownVisible = false;
		bIsAdvancedFilterConfiguratorVisible = false;
		bIsHierarchyBreadcrumbTrailVisible = false;
		bHideObjectsExternalToProject = true;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SObjectTableTreeView::~SObjectTableTreeView()
{
	TArray<IObjectProfilerExtender*> Extenders = IModularFeatures::Get().GetModularFeatureImplementations<IObjectProfilerExtender>(ObjectProfilerExtenderFeatureName);
	for (IObjectProfilerExtender* Extender : Extenders)
	{
		if (Extender)
		{
			Extender->OnEndSession(*this);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectTableTreeView::Construct(const FArguments& InArgs)
{
	TSharedRef<FObjectTable> TablePtr = MakeShared<FObjectTable>(bIsSimplifiedMode, bCanShowReferencingActors);
	TablePtr->Reset();
	TablePtr->SetDisplayName(LOCTEXT("ObjectsTableName", "Objects"));

	ConstructWidget(TablePtr);

	// Apply the default preset.
	check(GetAvailableViewPresets() != nullptr);
	check(GetAvailableViewPresets()->Num() > 0);
	ApplyViewPreset(*(*GetAvailableViewPresets())[0]);

	UpdateSelectionStatsText();

	TArray<IObjectProfilerExtender*> Extenders = IModularFeatures::Get().GetModularFeatureImplementations<IObjectProfilerExtender>(ObjectProfilerExtenderFeatureName);
	for (IObjectProfilerExtender* Extender : Extenders)
	{
		if (Extender)
		{
			Extender->OnBeginSession(*this);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SObjectTableTreeView::ConstructHeaderArea(TSharedRef<SVerticalBox> InHostBox)
{
	//////////////////////////////////////////////////
	// Snapshot & View Preset

	TSharedRef<SHorizontalBox> TopLine = SNew(SHorizontalBox);
	ConstructSnapshotSelector(TopLine);
	TopLine->AddSlot()
		.FillWidth(1.0f)
		.Padding(0.0f, 0.0f, 6.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
		];
	ConstructViewPreset(TopLine, 140.0f);

	InHostBox->AddSlot()
		.AutoHeight()
		.Padding(4.0f, 6.0f, 4.0f, 0.0f)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return bIsViewPresetsDropDownVisible ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				TopLine
			]
		];

	//////////////////////////////////////////////////
	// Filters

	InHostBox->AddSlot()
		.AutoHeight()
		.Padding(4.0f, 6.0f, 4.0f, 2.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 2.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SComboButton)
				.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
				.OnGetMenuContent(this, &SObjectTableTreeView::MakeFiltersMenu)
				.ToolTipText(LOCTEXT("FiltersMenuToolTip", "Filters"))
				.ContentPadding(FMargin(0.0f, 1.0f))
				.ButtonContent()
				[
					SNew(SImage)
					.Image(FInsightsStyle::GetBrush("Icons.Filter.ToolBar"))
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				ConstructSearchBox()
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.Visibility_Lambda([this]() { return bIsAdvancedFilterConfiguratorVisible ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					ConstructFilterConfiguratorButton()
				]
			]

#if 0
			// Visibility toggle for the Object Details panel
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ContentPadding(FMargin(0.0f, 0.0f, 0.0f, 0.0f))
				.OnClicked_Lambda([this]() -> FReply
					{
						ToggleObjectDetailsViewVisibility();
						return FReply::Handled();
					})
				[
					SNew(SImage)
					.Image_Lambda([this]() -> const FSlateBrush*
						{
							if (IsObjectDetailsViewVisible())
							{
								return FInsightsStyle::Get().GetBrush("Icons.FindPrevious.ToolBar");
							}
							else
							{
								return FInsightsStyle::Get().GetBrush("Icons.FindNext.ToolBar");
							}
						})
				]
			]
#endif
		];

	//////////////////////////////////////////////////
	// Grouping

	InHostBox->AddSlot()
		.AutoHeight()
		.Padding(4.0f, 2.0f, 4.0f, 4.0f)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return bIsHierarchyBreadcrumbTrailVisible ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				ConstructHierarchyBreadcrumbTrail()
			]
		];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedPtr<SWidget> SObjectTableTreeView::ConstructFooter()
{
#if INSIGHTS_SHOW_FOOTER
	return SNew(SHorizontalBox)

	+ SHorizontalBox::Slot()
	.FillWidth(1.0f)

	+ SHorizontalBox::Slot()
	.AutoWidth()
	.Padding(0.0f, 2.0f, 0.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(this, &SObjectTableTreeView::GetNumSelectedObjectsText)
	]

	+ SHorizontalBox::Slot()
	.AutoWidth()
	.Padding(4.0f, 2.0f, 0.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(this, &SObjectTableTreeView::GetSelectedObjectsText)
		.ColorAndOpacity(FSlateColor(EStyleColor::White25))
	]

	+ SHorizontalBox::Slot()
	.AutoWidth()
	.Padding(0.0f, 2.0f, 0.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(TEXT(" (StructSize: ")))
		.ColorAndOpacity(FSlateColor(EStyleColor::White25))
		.Visibility_Lambda([this]() { return !NumSelectedObjectsText.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
	]

	+ SHorizontalBox::Slot()
	.AutoWidth()
	.Padding(0.0f, 2.0f, 0.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(this, &SObjectTableTreeView::GetSelectionStructSizeText)
	]

	+ SHorizontalBox::Slot()
	.AutoWidth()
	.Padding(0.0f, 2.0f, 0.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(TEXT(", SystemMemSize: ")))
		.ColorAndOpacity(FSlateColor(EStyleColor::White25))
		.Visibility_Lambda([this]() { return !NumSelectedObjectsText.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
	]

	+ SHorizontalBox::Slot()
	.AutoWidth()
	.Padding(0.0f, 2.0f, 0.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(this, &SObjectTableTreeView::GetSelectionSystemMemSizeText)
	]

	+ SHorizontalBox::Slot()
	.AutoWidth()
	.Padding(0.0f, 2.0f, 0.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(TEXT(", VideoMemSize: ")))
		.ColorAndOpacity(FSlateColor(EStyleColor::White25))
		.Visibility_Lambda([this]() { return !NumSelectedObjectsText.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
	]

	+ SHorizontalBox::Slot()
	.AutoWidth()
	.Padding(0.0f, 2.0f, 0.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(this, &SObjectTableTreeView::GetSelectionVideoMemSizeText)
	]

	+ SHorizontalBox::Slot()
	.AutoWidth()
	.Padding(0.0f, 2.0f, 0.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(TEXT(")")))
		.ColorAndOpacity(FSlateColor(EStyleColor::White25))
		.Visibility_Lambda([this]() { return !NumSelectedObjectsText.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
	]

	+ SHorizontalBox::Slot()
	.FillWidth(1.0f);
#else // INSIGHTS_SHOW_FOOTER
	return nullptr;
#endif // INSIGHTS_SHOW_FOOTER
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// Tree View
////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectTableTreeView::UpdateSelectionStatsText()
{
	TArray<FTableTreeNodePtr> SelectedNodes;
	const int32 NumSelectedNodes = TreeView->GetSelectedItems(SelectedNodes);

#if INSIGHTS_SHOW_FOOTER
	if (NumSelectedNodes > 0)
	{
		int64 TotalCount = 0;
		int64 TotalStructSize = 0;
		int64 TotalSystemMemSize = 0;
		int64 TotalVideoMemSize = 0;
		for (const FTableTreeNodePtr& SelectedNode : SelectedNodes)
		{
			if (SelectedNode->Is<FObjectNode>())
			{
				const FObjectNode& ObjectNode = SelectedNode->As<FObjectNode>();
				++TotalCount;
				TotalStructSize += ObjectNode.GetStructureSize();
				TotalSystemMemSize += ObjectNode.GetSystemMemorySize();
				TotalVideoMemSize += ObjectNode.GetVideoMemorySize();
			}
		}

		FNumberFormattingOptions FormattingOptionsMem;
		FormattingOptionsMem.MaximumFractionalDigits = 2;

		NumSelectedObjectsText = FText::AsNumber(TotalCount);
		SelectedObjectsText = FText::Format(LOCTEXT("SelectionStatsFmt", "selected {0}|plural(one=object,other=objects)"), FText::AsNumber(TotalCount));
		SelectionStructSizeText = TotalStructSize == 0 ? FText::FromString(TEXT("0")) : FText::AsMemory(TotalStructSize, &FormattingOptionsMem);
		SelectionSystemMemSizeText = TotalSystemMemSize == 0 ? FText::FromString(TEXT("0")) : FText::AsMemory(TotalSystemMemSize, &FormattingOptionsMem);
		SelectionVideoMemSizeText = TotalVideoMemSize == 0 ? FText::FromString(TEXT("0")) : FText::AsMemory(TotalVideoMemSize, &FormattingOptionsMem);
	}
	else
	{
		NumSelectedObjectsText = FText::GetEmpty();
		SelectedObjectsText = LOCTEXT("NoSelectionStats", "No object selected");
		SelectionStructSizeText = FText::GetEmpty();
		SelectionSystemMemSizeText = FText::GetEmpty();
		SelectionVideoMemSizeText = FText::GetEmpty();
	}
#endif // INSIGHTS_SHOW_FOOTER

	if (TSharedPtr<SObjectDetailsView> ObjectDetailsView = WeakObjectDetailsView.Pin())
	{
		ObjectDetailsView->SetTableTreeView(SharedThis(this));
		ObjectDetailsView->SetSelectedNodes(SelectedNodes);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectTableTreeView::TreeView_OnSelectionChanged(FTableTreeNodePtr SelectedItem, ESelectInfo::Type SelectInfo)
{
	UpdateSelectionStatsText();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectTableTreeView::TreeView_OnMouseButtonDoubleClick(FTableTreeNodePtr NodePtr)
{
	if (NodePtr->IsGroup())
	{
		const bool bIsGroupExpanded = TreeView->IsItemExpanded(NodePtr);
		TreeView->SetItemExpansion(NodePtr, !bIsGroupExpanded);
	}
	else if (NodePtr->Is<FObjectNode>())
	{
		FObjectNode& ObjectNode = NodePtr->As<FObjectNode>();
		//...
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Snapshot
////////////////////////////////////////////////////////////////////////////////////////////////////

FText FObjectSnapshotInfo::GetName() const
{
	return FText::Format(LOCTEXT("SnapshotNameFmt", "{0} ({1} objects) -- {2}"),
		FText::FromString(*UE::Insights::FormatTimeAuto(StartTime)),
		FText::AsNumber(ObjectCount),
		bHasTotalEstimatedMemory ?
		LOCTEXT("SnapshotHasTotalEstimatedMemory", "with total estimated memory") :
		LOCTEXT("SnapshotHasExclusiveEstimatedMemory", "with exclusive estimated memory"));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FObjectSnapshotInfo::GetToolTip() const
{
	return FText::Format(LOCTEXT("SnapshotToolTipFmt", "Snapshot Id: {0}\nTimestamp: {1}\nObjects: {2}\nEstimated Memory Mode: {3}"),
		Id,
		FText::FromString(*UE::Insights::FormatTimeAuto(StartTime)),
		FText::AsNumber(ObjectCount),
		bHasTotalEstimatedMemory ?
		LOCTEXT("SnapshotHasTotalEstimatedMemory2", "total") :
		LOCTEXT("SnapshotHasExclusiveEstimatedMemory2", "exclusive"));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectTableTreeView::UpdateAvailableSnapshots()
{
	const FObjectSnapshotInfo* PreviousSelectedSnapshot = SelectedSnapshot.Get();
	uint32 AvailableSnapshotCount = 0;

	if (Session.IsValid())
	{
		const TraceServices::IObjectProvider* ObjectProvider = TraceServices::ReadObjectProvider(*Session.Get());
		if (ObjectProvider)
		{
			TraceServices::FProviderReadScopeLock ProviderReadScope(*ObjectProvider);

			AvailableSnapshotCount = ObjectProvider->GetNumSnapshots();

			if (AvailableSnapshotCount != uint32(AvailableSnapshots.Num()))
			{
				check(AvailableSnapshotCount > uint32(AvailableSnapshots.Num()));
				for (uint32 SnapshotId = AvailableSnapshots.Num(); SnapshotId < AvailableSnapshotCount; ++SnapshotId)
				{
					TSharedRef<FObjectSnapshotInfo> SnapshotInfo = MakeShared<FObjectSnapshotInfo>();
					SnapshotInfo->Id = SnapshotId;
					const TraceServices::IObjectSnapshot* Snapshot = ObjectProvider->GetSnapshot(SnapshotId);
					if (Snapshot)
					{
						SnapshotInfo->StartTime = Snapshot->GetStartTime();
						SnapshotInfo->EndTime = Snapshot->GetEndTime();
						SnapshotInfo->ObjectCount = Snapshot->GetObjectArrayNum();
						SnapshotInfo->bHasTotalEstimatedMemory = Snapshot->HasTotalMemorySizes();
					}
					AvailableSnapshots.Add(MoveTemp(SnapshotInfo));
				}
				check(AvailableSnapshotCount == uint32(AvailableSnapshots.Num()));
			}
		}
	}

	if (AvailableSnapshotCount == 0)
	{
		AvailableSnapshots.Reset();
		SelectedSnapshot.Reset();
	}
	else if (!SelectedSnapshot)
	{
		check(AvailableSnapshotCount == AvailableSnapshots.Num());
		SelectedSnapshot = AvailableSnapshots[0];
	}

	if (SelectedSnapshot.Get() != PreviousSelectedSnapshot)
	{
		RebuildTree(true);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SObjectTableTreeView::ConstructSnapshotSelector(TSharedPtr<SHorizontalBox> Box)
{
	Box->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Snapshot", "Snapshot:"))
		];

	Box->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.MinDesiredWidth(100.0f)
			[
				SAssignNew(SnapshotComboBox, SComboBox<TSharedRef<FObjectSnapshotInfo>>)
				.ToolTipText(this, &SObjectTableTreeView::Snapshot_GetSelectedToolTipText)
				.OptionsSource(&AvailableSnapshots)
				.OnSelectionChanged(this, &SObjectTableTreeView::Snapshot_OnSelectionChanged)
				.OnGenerateWidget(this, &SObjectTableTreeView::Snapshot_OnGenerateWidget)
				[
					SNew(STextBlock)
					.Text(this, &SObjectTableTreeView::Snapshot_GetSelectedText)
				]
			]
		];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectTableTreeView::Snapshot_OnSelectionChanged(TSharedPtr<FObjectSnapshotInfo> InSnapshot, ESelectInfo::Type SelectInfo)
{
	SelectedSnapshot = InSnapshot;
	RebuildTree(false);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> SObjectTableTreeView::Snapshot_OnGenerateWidget(TSharedRef<FObjectSnapshotInfo> InSnapshot)
{
	return SNew(STextBlock)
		.Text(InSnapshot->GetName())
		.ToolTipText(InSnapshot->GetToolTip())
		.Margin(2.0f);
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SObjectTableTreeView::Snapshot_GetSelectedText() const
{
	return SelectedSnapshot ? SelectedSnapshot->GetName() : LOCTEXT("NoSnapshot", "N/A");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SObjectTableTreeView::Snapshot_GetSelectedToolTipText() const
{
	return SelectedSnapshot ? SelectedSnapshot->GetToolTip() : LOCTEXT("NoSnapshot_ToolTip", "No Snapshot Available");
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Details View
////////////////////////////////////////////////////////////////////////////////////////////////////

bool SObjectTableTreeView::IsObjectDetailsViewVisible() const
{
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectTableTreeView::ToggleObjectDetailsViewVisibility()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Filters
////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectTableTreeView::SetHideObjectsExternalToProject(bool bHide)
{
	if (bHideObjectsExternalToProject != bHide)
	{
		bHideObjectsExternalToProject = bHide;
		OnNodeFilteringChanged();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SObjectTableTreeView::MakeFiltersMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	MenuBuilder.SetSearchable(false);

	MenuBuilder.BeginSection("Filters", LOCTEXT("ContextMenu_Section_Filters", "Filters"));

	if (bIsSimplifiedMode)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("HideObjectsExternalToProject", "Hide objects External to the Project"),
			LOCTEXT("HideObjectsExternalToProject_TT", "Hides objects that are not part of the current project."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]
					{
						bHideObjectsExternalToProject = !bHideObjectsExternalToProject;
						OnNodeFilteringChanged();
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda(
					[this]() -> bool
					{
						return bHideObjectsExternalToProject;
					})),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);
	}

	if (bCanShowReferencingActors)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("HideObjectsWithZeroReferencingActors", "Hide objects with No Referencing Actors"),
			LOCTEXT("HideObjectsWithZeroReferencingActors_TT", "Hides the objects that are not referenced by any Actor."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]
					{
						bHideObjectsWithZeroReferencingActors = !bHideObjectsWithZeroReferencingActors;
						OnNodeFilteringChanged();
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda(
					[this]() -> bool
					{
						return bHideObjectsWithZeroReferencingActors;
					})),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);
	}

	if (!bIsSimplifiedMode || !bHasTotalEstimatedMemory)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("HideObjectsWithZeroEstimatedMemory", "Hide objects with No Estimated Memory"),
			LOCTEXT("HideObjectsWithZeroEstimatedMemory_TT", "Hides the objects with no estimated memory (System + Video)."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]
					{
						bHideObjectsWithZeroEstimatedMemory = !bHideObjectsWithZeroEstimatedMemory;
						OnNodeFilteringChanged();
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda(
					[this]() -> bool
					{
						return bHideObjectsWithZeroEstimatedMemory;
					})),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("HideObjectsWithLowEstimatedMemory", "Hide objects with Low Estimated Memory"),
			LOCTEXT("HideObjectsWithLowEstimatedMemory_TT", "Hides the objects with exclusive estimated memory (system + video) < 1 MiB."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]
					{
						bHideObjectsWithLowEstimatedMemory = !bHideObjectsWithLowEstimatedMemory;
						OnNodeFilteringChanged();
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda(
					[this]() -> bool
					{
						return bHideObjectsWithLowEstimatedMemory;
					})),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("HideObjectsWithLowImpact", "Hide objects with Low Impact"),
			LOCTEXT("HideObjectsWithLowImpact_TT", "Hides the objects with impact % < 1%."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]
					{
						bHideObjectsWithLowImpact = !bHideObjectsWithLowImpact;
						OnNodeFilteringChanged();
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda(
					[this]() -> bool
					{
						return bHideObjectsWithLowImpact;
					})),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);
	}

	if (!bIsSimplifiedMode || bHasTotalEstimatedMemory)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("HideObjectsWithZeroTotalEstimatedMemory", "Hide objects with No Total Estimated Memory"),
			LOCTEXT("HideObjectsWithZeroTotalEstimatedMemory_TT", "Hides the objects with no total estimated memory (System + Video)."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]
					{
						bHideObjectsWithZeroTotalEstimatedMemory = !bHideObjectsWithZeroTotalEstimatedMemory;
						OnNodeFilteringChanged();
					}),
				FCanExecuteAction::CreateLambda(
					[this]
					{
						return bHasTotalEstimatedMemory;
					}),
				FIsActionChecked::CreateLambda(
					[this]() -> bool
					{
						return bHideObjectsWithZeroTotalEstimatedMemory;
					})),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("HideObjectsWithLowTotalEstimatedMemory", "Hide objects with Low Total Estimated Memory"),
			LOCTEXT("HideObjectsWithLowTotalEstimatedMemory_TT", "Hides the objects with total estimated memory (system + video) < 1 MiB."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]
					{
						bHideObjectsWithLowTotalEstimatedMemory = !bHideObjectsWithLowTotalEstimatedMemory;
						OnNodeFilteringChanged();
					}),
				FCanExecuteAction::CreateLambda(
					[this]
					{
						return bHasTotalEstimatedMemory;
					}),
				FIsActionChecked::CreateLambda(
					[this]() -> bool
					{
						return bHideObjectsWithLowTotalEstimatedMemory;
					})),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("HideObjectsWithLowTotalImpact", "Hide objects with Low Total Impact"),
			LOCTEXT("HideObjectsWithLowTotalImpact_TT", "Hides the objects with total impact % < 1%."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]
					{
						bHideObjectsWithLowTotalImpact = !bHideObjectsWithLowTotalImpact;
						OnNodeFilteringChanged();
					}),
				FCanExecuteAction::CreateLambda(
					[this]
					{
						return bHasTotalEstimatedMemory;
					}),
				FIsActionChecked::CreateLambda(
					[this]() -> bool
					{
						return bHideObjectsWithLowTotalImpact;
					})),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);
	}

	if (!bIsSimplifiedMode)
	{
		MenuBuilder.AddMenuSeparator();

		MenuBuilder.AddMenuEntry(
			LOCTEXT("HidePackages", "Hide packages"),
			LOCTEXT("HidePackages_TT", "Hides the UPackage objects."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]
					{
						bHidePackages = !bHidePackages;
						OnNodeFilteringChanged();
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda(
					[this]() -> bool
					{
						return bHidePackages;
					})),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("HideSubObjects", "Hide sub-objects"),
			LOCTEXT("HideSubObjects_TT", "Hides the sub-objects."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]
					{
						bHideSubObjects = !bHideSubObjects;
						OnNodeFilteringChanged();
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda(
					[this]() -> bool
					{
						return bHideSubObjects;
					})),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);
	}

	if (!bIsSimplifiedMode && bShowAdvancedUI)
	{
		MenuBuilder.AddMenuSeparator();

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowOnlyFieldObjects", "Show only the UField objects"),
			LOCTEXT("ShowOnlyFieldObjects_TT", "Shows only the UField objects (including UStruct objects)."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]
					{
						bShowOnlyFieldObjects = !bShowOnlyFieldObjects;
						bFilterByClassType = bShowOnlyFieldObjects || bShowOnlyStructObjects || bShowOnlyClassObjects || bShowOnlyFunctionObjects || bShowOnlyPackageObjects;
						OnNodeFilteringChanged();
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda(
					[this]() -> bool
					{
						return bShowOnlyFieldObjects;
					})),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowOnlyStructObjects", "Show only the UStruct objects"),
			LOCTEXT("ShowOnlyStructObjects_TT", "Shows only the UStruct objects (including UClass and UFunction objects)."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]
					{
						bShowOnlyStructObjects = !bShowOnlyStructObjects;
						bFilterByClassType = bShowOnlyFieldObjects || bShowOnlyStructObjects || bShowOnlyClassObjects || bShowOnlyFunctionObjects || bShowOnlyPackageObjects;
						OnNodeFilteringChanged();
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda(
					[this]() -> bool
					{
						return bShowOnlyStructObjects;
					})),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowOnlyClassObjects", "Show only the UClass objects"),
			LOCTEXT("ShowOnlyClassObjects_TT", "Shows only the UClass objects."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]
					{
						bShowOnlyClassObjects = !bShowOnlyClassObjects;
						bFilterByClassType = bShowOnlyFieldObjects || bShowOnlyStructObjects || bShowOnlyClassObjects || bShowOnlyFunctionObjects || bShowOnlyPackageObjects;
						OnNodeFilteringChanged();
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda(
					[this]() -> bool
					{
						return bShowOnlyClassObjects;
					})),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowOnlyFunctionObjects", "Show only the UFunction objects"),
			LOCTEXT("ShowOnlyFunctionObjects_TT", "Shows only the UFunction objects."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]
					{
						bShowOnlyFunctionObjects = !bShowOnlyFunctionObjects;
						bFilterByClassType = bShowOnlyFieldObjects || bShowOnlyStructObjects || bShowOnlyClassObjects || bShowOnlyFunctionObjects || bShowOnlyPackageObjects;
						OnNodeFilteringChanged();
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda(
					[this]() -> bool
					{
						return bShowOnlyFunctionObjects;
					})),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowOnlyPackageObjects", "Show only the UPackage objects"),
			LOCTEXT("ShowOnlyPackageObjects_TT", "Shows only the UPackage objects."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]
					{
						bShowOnlyPackageObjects = !bShowOnlyPackageObjects;
						bFilterByClassType = bShowOnlyFieldObjects || bShowOnlyStructObjects || bShowOnlyClassObjects || bShowOnlyFunctionObjects || bShowOnlyPackageObjects;
						OnNodeFilteringChanged();
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda(
					[this]() -> bool
					{
						return bShowOnlyPackageObjects;
					})),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);
	}

	if (!bIsSimplifiedMode)
	{
		MenuBuilder.AddMenuSeparator();

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowAdvancedUI", "Show Advanced UI"),
			LOCTEXT("ShowAdvancedUI_TT", "Shows the advanced UI options (ex.: the advanced filter configurator and the grouping breadcrumb trail)."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]
					{
						if (bShowAdvancedUI)
						{
							bShowAdvancedUI = false;
							bIsViewPresetsDropDownVisible = false;
							bIsAdvancedFilterConfiguratorVisible = false;
							bIsHierarchyBreadcrumbTrailVisible = false;
						}
						else
						{
							bShowAdvancedUI = true;
							bIsViewPresetsDropDownVisible = true;
							bIsAdvancedFilterConfiguratorVisible = true;
							bIsHierarchyBreadcrumbTrailVisible = true;
						}
						OnNodeFilteringChanged();
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda(
					[this]() -> bool
					{
						return bShowAdvancedUI;
					})),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);
	}

	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Grouping
////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectTableTreeView::InternalCreateGroupings()
{
	STableTreeView::InternalCreateGroupings();

	AvailableGroupings.RemoveAll(
		[](TSharedPtr<FTreeNodeGrouping>& Grouping)
		{
			if (Grouping->Is<FTreeNodeGroupingByUniqueValue>())
			{
				const FName ColumnId = Grouping->As<FTreeNodeGroupingByUniqueValue>().GetColumnId();
				return (ColumnId == FObjectTableColumns::ObjectIdColumnId);
			}
			return false;
		});

	int32 Index = 1; // after the Flat ("All") grouping

	auto ClassCategoryGrouping = MakeShared<FObjectGroupingByClassCategory>(SharedThis(this));
	ClassCategoryGrouping->SetShouldHideObjectBaseClass(true);
	ClassCategoryGrouping->SetShouldUpdateSegmentedBarGraph(true);
	AvailableGroupings.Insert(ClassCategoryGrouping, Index++);

	auto ClassHierarchyGrouping = MakeShared<FObjectGroupingByClassHierarchy>(SharedThis(this));
	ClassHierarchyGrouping->SetShouldHideObjectBaseClass(true);
	ClassHierarchyGrouping->SetShouldUpdateSegmentedBarGraph(false);
	AvailableGroupings.Insert(ClassHierarchyGrouping, Index++);

	AvailableGroupings.Insert(MakeShared<FObjectGroupingByObjectName>(), Index++);
	AvailableGroupings.Insert(MakeShared<FObjectGroupingByOuter>(), Index++);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectTableTreeView::InitAvailableViewPresets()
{
	AvailableViewPresets.Add(FObjectTableViewPresets::CreateAssetViewPreset(*this));
	AvailableViewPresets.Add(FObjectTableViewPresets::CreateObjectViewPreset(*this));
	AvailableViewPresets.Add(FObjectTableViewPresets::CreateClassViewPreset(*this));
	AvailableViewPresets.Add(FObjectTableViewPresets::CreateOuterViewPreset(*this));

	SelectedViewPreset = AvailableViewPresets[0];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectTableTreeView::PostApplyAssetsViewPreset()
{
	check(IsInGameThread());

	if (bHasTotalEstimatedMemory)
	{
		bHideObjectsWithZeroEstimatedMemory = false;
		bHideObjectsWithLowEstimatedMemory = false;
		bHideObjectsWithLowImpact = false;

		bHideObjectsWithZeroTotalEstimatedMemory = true;
		bHideObjectsWithLowTotalEstimatedMemory = false;
		bHideObjectsWithLowTotalImpact = false;

		bHidePackages = true;
		bHideSubObjects = true;

		HideColumn(FObjectTableColumns::EstimatedSizeColumnId);
		HideColumn(FObjectTableColumns::ImpactColumnId);
		ShowColumn(FObjectTableColumns::TotalEstimatedSizeColumnId);
		ShowColumn(FObjectTableColumns::TotalImpactColumnId);

		ColumnBeingSorted = FObjectTableColumns::TotalEstimatedSizeColumnId;
		ColumnSortMode = EColumnSortMode::Descending;
		UpdateCurrentSortingByColumn();
	}
	else
	{
		bHideObjectsWithZeroEstimatedMemory = true;
		bHideObjectsWithLowEstimatedMemory = false;
		bHideObjectsWithLowImpact = false;

		bHideObjectsWithZeroTotalEstimatedMemory = false;
		bHideObjectsWithLowTotalEstimatedMemory = false;
		bHideObjectsWithLowTotalImpact = false;

		bHidePackages = false;
		bHideSubObjects = false;

		ShowColumn(FObjectTableColumns::EstimatedSizeColumnId);
		ShowColumn(FObjectTableColumns::ImpactColumnId);
		HideColumn(FObjectTableColumns::TotalEstimatedSizeColumnId);
		HideColumn(FObjectTableColumns::TotalImpactColumnId);

		ColumnBeingSorted = FObjectTableColumns::EstimatedSizeColumnId;
		ColumnSortMode = EColumnSortMode::Descending;
		UpdateCurrentSortingByColumn();
	}

	UpdateTree();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectTableTreeView::Reset()
{
	CurrentSnapshotId = uint32(-1);
	NextTimestamp = 0;
	AvailableSnapshots.Empty();
	STableTreeView::Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectTableTreeView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	STableTreeView::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// We need to check if the list of UObject snapshots has changed.
	// But, ensure we do not check too often.
	uint64 Time = FPlatformTime::Cycles64();
	if (Time > NextTimestamp && !bIsUpdateRunning)
	{
		UpdateAvailableSnapshots();

		const uint64 WaitTime = static_cast<uint64>(3.0 / FPlatformTime::GetSecondsPerCycle64()); // 3 seconds
		NextTimestamp = Time + WaitTime;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectTableTreeView::RebuildTree(bool bResync)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SObjectTableTreeView::RebuildTree);

	FStopwatch Stopwatch;
	Stopwatch.Start();

	FStopwatch SyncStopwatch;
	SyncStopwatch.Start();

	bool bListHasChanged = false;

	const uint32 PendingSnapshotId = SelectedSnapshot ? SelectedSnapshot->Id : InvalidSnapshotId;
	if (bResync || PendingSnapshotId != CurrentSnapshotId)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RebuildTree_Resync);

		bListHasChanged = true;
		ObjectNodes.Empty();
		Classes.Empty();
		Packages.Empty();
		TableRowNodes.Empty();
		CurrentSnapshotId = PendingSnapshotId;
		bHasTotalEstimatedMemory = false;

		if (Session.IsValid())
		{
			const TraceServices::IObjectProvider* ObjectProvider = TraceServices::ReadObjectProvider(*Session.Get());
			if (ObjectProvider)
			{
				TraceServices::FProviderReadScopeLock ProviderReadScope(*ObjectProvider);

				const TraceServices::IObjectSnapshot* Snapshot = ObjectProvider->GetSnapshot(PendingSnapshotId);
				if (Snapshot)
				{
					bHasTotalEstimatedMemory = Snapshot->HasTotalMemorySizes();

					const uint32 ObjectCount = Snapshot->GetObjectArrayNum();
					ObjectNodes.AddDefaulted(ObjectCount); // index == object id

					uint32 ValidObjectCount = 0;
					uint32 PackageCount = 0;
					uint32 FunctionCount = 0;
					uint32 ClassCount = 0;
					uint32 StructCount = 0;
					uint32 FieldCount = 0;

					{
						TRACE_CPUPROFILER_EVENT_SCOPE(ResyncTree_CreateNodes);

						TSharedPtr<FObjectTable> ObjectTable = StaticCastSharedPtr<FObjectTable>(GetTable());
						for (uint32 ObjectId = 0; ObjectId < ObjectCount; ++ObjectId)
						{
							const TraceServices::FObjectInfo* Object = Snapshot->GetObject(ObjectId);
							if (Object && Object->Id == ObjectId)
							{
								TSharedPtr<FObjectNode> ObjectNode;
								if (EnumHasAnyFlags(Object->FlagsEx, TraceServices::EObjectInfoFlags::IsPackage))
								{
									auto PackageNode = MakeShared<FPackageObjectNode>(ObjectTable, *Object);
									Packages.Add(PackageNode);
									ObjectNode = PackageNode;
									++PackageCount;
								}
								else if (EnumHasAnyFlags(Object->FlagsEx, TraceServices::EObjectInfoFlags::IsFunction))
								{
									auto FunctionNode = MakeShared<FFunctionObjectNode>(ObjectTable, *Object);
									ObjectNode = FunctionNode;
									++FunctionCount;
								}
								else if (EnumHasAnyFlags(Object->FlagsEx, TraceServices::EObjectInfoFlags::IsClass))
								{
									auto ClassNode = MakeShared<FClassObjectNode>(ObjectTable, *Object);
									Classes.Add(ClassNode);
									ObjectNode = ClassNode;
									++ClassCount;
								}
								else if (EnumHasAnyFlags(Object->FlagsEx, TraceServices::EObjectInfoFlags::IsStruct))
								{
									auto StructNode = MakeShared<FStructObjectNode>(ObjectTable, *Object);
									ObjectNode = StructNode;
									++StructCount;
								}
								else if (EnumHasAnyFlags(Object->FlagsEx, TraceServices::EObjectInfoFlags::IsField))
								{
									auto FieldNode = MakeShared<FFieldObjectNode>(ObjectTable, *Object);
									ObjectNode = FieldNode;
									++FieldCount;
								}
								else
								{
									ObjectNode = MakeShared<FObjectNode>(ObjectTable, *Object);
								}
								ObjectNodes[ObjectId] = ObjectNode;
								++ValidObjectCount;
							}
						}
					}

					UE_LOGF(LogObjectProfiler, Log, "[Obj] Created nodes for %u objects (%u packages, %u functions, %u classes, %u structs, %u fields)",
						ValidObjectCount, PackageCount, FunctionCount, ClassCount, StructCount, FieldCount);

					{
						TRACE_CPUPROFILER_EVENT_SCOPE(ResyncTree_UpdateTableRowNodes);

						TableRowNodes.Reserve(ValidObjectCount);

						// All object nodes are created...
						for (TSharedPtr<FObjectNode>& NodePtr : ObjectNodes)
						{
							if (!NodePtr.IsValid())
							{
								continue;
							}

							// ...Now we can resolve pointers to class and outer objects.
							NodePtr->SetClass(GetObjectNode(NodePtr->GetClassId()));
							NodePtr->SetOuter(GetObjectNode(NodePtr->GetOuterId()));

							// Also add the valid pointers to the tree view's list of nodes.
							TableRowNodes.Add(NodePtr);
						}
					}

					{
						TRACE_CPUPROFILER_EVENT_SCOPE(ResyncTree_UpdateObjectPaths);

						// The outer objects are set...
						for (TSharedPtr<FObjectNode>& NodePtr : ObjectNodes)
						{
							if (!NodePtr.IsValid())
							{
								continue;
							}

							// ...Now we can init the object paths.
							NodePtr->InitObjectPath();
						}
					}

					TSharedPtr<IAssetInfoProvider> AssetInfoProvider = WeakAssetInfoProvider.Pin();
					if (AssetInfoProvider.IsValid())
					{
						TSet<FName> PackageNamesSet;

						{
							TRACE_CPUPROFILER_EVENT_SCOPE(ResyncTree_UpdateAssetData);

							for (TSharedPtr<FObjectNode>& NodePtr : ObjectNodes)
							{
								if (!NodePtr.IsValid())
								{
									continue;
								}

								NodePtr->ConvertObjectPath(AssetInfoProvider);

								if (NodePtr->GetEstimatedMemorySize() == 0 &&
									NodePtr->GetTotalEstimatedMemorySize() == 0)
								{
									continue;
								}

								if (bHasTotalEstimatedMemory)
								{
									TSharedPtr<FObjectNode> Outer = NodePtr->GetOuter();
									const bool bIsPackage = !Outer.IsValid();
									if (bIsPackage)
									{
										continue;
									}
									const bool bIsSubObject = Outer.IsValid() && Outer->GetOuter().IsValid();
									if (bIsSubObject)
									{
										continue;
									}
								}

								FAssetData Data;
								{
									TRACE_CPUPROFILER_EVENT_SCOPE(ResyncTree_GetAssetData);
									AssetInfoProvider->GetAssetData(*NodePtr, Data);
								}
								if (Data.IsValid() && !Data.PackageName.IsNone())
								{
									if (!ShouldSkipActorAttribution(Data))
									{
										PackageNamesSet.Add(Data.PackageName);
									}
									NodePtr->SetMatchedAsset(MoveTemp(Data));
								}
							}
						}

						TMap<FName, TSharedRef<FActorSet>> NameToActorMap;

						FStopwatch MatchStopwatch;
						MatchStopwatch.Start();
						{
							TRACE_CPUPROFILER_EVENT_SCOPE(ResyncTree_MatchNamesToActors);
							TArray<FName> PackageNames = PackageNamesSet.Array();
							NameToActorMap = AssetInfoProvider->MatchNamesToActors(PackageNames);
						}
						MatchStopwatch.Stop();
						UE_LOGF(LogObjectProfiler, Log,
							"[Obj] MatchNamesToActors: %d packages, %d actors matched in %.4fs",
							PackageNamesSet.Num(), NameToActorMap.Num(), MatchStopwatch.GetAccumulatedTime());

						{
							TRACE_CPUPROFILER_EVENT_SCOPE(ResyncTree_UpdateMatchedActors);

							for (TSharedPtr<FObjectNode>& NodePtr : ObjectNodes)
							{
								if (!NodePtr.IsValid())
								{
									continue;
								}

								if (NodePtr->GetEstimatedMemorySize() == 0 &&
									NodePtr->GetTotalEstimatedMemorySize() == 0)
								{
									continue;
								}

								if (bHasTotalEstimatedMemory)
								{
									TSharedPtr<FObjectNode> Outer = NodePtr->GetOuter();
									const bool bIsPackage = !Outer.IsValid();
									if (bIsPackage)
									{
										continue;
									}
									const bool bIsSubObject = Outer.IsValid() && Outer->GetOuter().IsValid();
									if (bIsSubObject)
									{
										continue;
									}
								}

								const FAssetData& MatchedAsset = NodePtr->GetMatchedAsset();
								if (!MatchedAsset.PackageName.IsNone())
								{
									TSharedRef<FActorSet>* ActorSet = NameToActorMap.Find(MatchedAsset.PackageName);
									if (ActorSet)
									{
										NodePtr->SetMatchedActors(*ActorSet);
									}
								}
							}
						}

						if (bIsSimplifiedMode && GMaskAssetNames)
						{
							TRACE_CPUPROFILER_EVENT_SCOPE(ResyncTree_UpdateIdentity);

							for (TSharedPtr<FObjectNode>& NodePtr : ObjectNodes)
							{
								if (!NodePtr.IsValid())
								{
									continue;
								}

								// Excludes Field objects. Implicitly excludes also Struct, Class and Function objects.
								if (NodePtr->IsField())
								{
									continue;
								}

								const FAssetData& MatchedAsset = NodePtr->GetMatchedAsset();
								if (MatchedAsset.IsValid())
								{
									NodePtr->SetIdentityMasked(AssetInfoProvider->ShouldMaskAssetIdentity(MatchedAsset));
								}
								else
								{
									NodePtr->SetIdentityMasked(true);
								}
							}
						}

						if (bIsSimplifiedMode)
						{
							TRACE_CPUPROFILER_EVENT_SCOPE(ResyncTree_SetOwnedByCurrentProject);

							for (TSharedPtr<FObjectNode>& NodePtr : ObjectNodes)
							{
								if (!NodePtr.IsValid())
								{
									continue;
								}

								const FAssetData& MatchedAsset = NodePtr->GetMatchedAsset();
								if (MatchedAsset.IsValid())
								{
									NodePtr->SetOwnedByCurrentProject(AssetInfoProvider->IsAssetOwnedByCurrentProject(MatchedAsset));
								}
							}
						}
					}
				}
			}
		}
	}

	SyncStopwatch.Stop();

	if (bListHasChanged)
	{
		// Save selection.
		TArray<FTableTreeNodePtr> SelectedItems;
		TreeView->GetSelectedItems(SelectedItems);

		TSharedPtr<ITableTreeViewPreset> Preset = GetSelectedViewPreset();
		if (Preset.IsValid() &&
			(Preset->GetName().ToString().Compare(FString("Assets")) == 0))
		{
			PostApplyAssetsViewPreset();
		}
		else
		{
			UpdateTree();
		}

		TreeView->RebuildList();

		// Restore selection.
		if (SelectedItems.Num() > 0)
		{
			TreeView->ClearSelection();
			for (FTableTreeNodePtr& NodePtr : SelectedItems)
			{
				if (!NodePtr->Is<FObjectNode>())
				{
					NodePtr.Reset();
					continue;
				}
				NodePtr = GetObjectNode(NodePtr->As<FObjectNode>().GetObjectId());
			}
			SelectedItems.RemoveAll([](const FTableTreeNodePtr& NodePtr) { return !NodePtr.IsValid(); });
			if (SelectedItems.Num() > 0)
			{
				TreeView->SetItemSelection(SelectedItems, true);
				TreeView->RequestScrollIntoView(SelectedItems.Last());
			}
		}
	}

	Stopwatch.Stop();
	const double TotalTime = Stopwatch.GetAccumulatedTime();
	if (TotalTime > 0.01)
	{
		const double SyncTime = SyncStopwatch.GetAccumulatedTime();
		UE_LOGF(LogObjectProfiler, Log, "[Obj] Tree view rebuilt in %.4fs (sync: %.4fs + update: %.4fs) --> %d objects",
			TotalTime, SyncTime, TotalTime - SyncTime, TableRowNodes.Num());
	}

	TArray<IObjectProfilerExtender*> Extenders = IModularFeatures::Get().GetModularFeatureImplementations<IObjectProfilerExtender>(ObjectProfilerExtenderFeatureName);
	for (IObjectProfilerExtender* Extender : Extenders)
	{
		if (Extender)
		{
			FOnSnapshotTreeRebuiltParams Params;
			Params.SnapshotId = CurrentSnapshotId;
			Params.RebuildDurationSeconds = TotalTime;
			Extender->OnSnapshotTreeRebuilt(Params);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectTableTreeView::ExtendMenu(TSharedRef<FExtender> Extender)
{
	Extender->AddMenuExtension("Misc", EExtensionHook::Before, nullptr, FMenuExtensionDelegate::CreateSP(this, &SObjectTableTreeView::ExtendMenuBeforeMisc));
	Extender->AddMenuExtension("Misc", EExtensionHook::After, nullptr, FMenuExtensionDelegate::CreateSP(this, &SObjectTableTreeView::ExtendMenuAfterMisc));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectTableTreeView::ExtendMenuBeforeMisc(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("UObject1", LOCTEXT("ContextMenu_Section_UObject1", "UObject 1"));
	{
	}
	MenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectTableTreeView::ExtendMenuAfterMisc(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("UObject2", LOCTEXT("ContextMenu_Section_UObject2", "UObject 2"));
	{
	}
	MenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SObjectTableTreeView::HasCustomNodeFilter() const
{
	return bFilterByClassType
		|| bHideObjectsWithZeroReferencingActors
		|| bHideObjectsWithZeroEstimatedMemory
		|| bHideObjectsWithLowEstimatedMemory
		|| bHideObjectsWithLowImpact
		|| bHideObjectsWithZeroTotalEstimatedMemory
		|| bHideObjectsWithLowTotalEstimatedMemory
		|| bHideObjectsWithLowTotalImpact
		|| bHidePackages
		|| bHideSubObjects
		|| bHideObjectsExternalToProject;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SObjectTableTreeView::FilterNodeCustom(const FTableTreeNode& InNode) const
{
	if (InNode.Is<FObjectNode>())
	{
		const FObjectNode& ObjectNode = InNode.As<FObjectNode>();

		if (bHideObjectsExternalToProject && !ObjectNode.IsOwnedByCurrentProject())
		{
			return false;
		}

		if (bFilterByClassType)
		{
			if (bShowOnlyFieldObjects)
			{
				if (!ObjectNode.Is<FFieldObjectNode>())
				{
					return false;
				}
			}
			if (bShowOnlyStructObjects)
			{
				if (!ObjectNode.Is<FStructObjectNode>())
				{
					return false;
				}
			}
			if (bShowOnlyClassObjects)
			{
				if (!ObjectNode.Is<FClassObjectNode>())
				{
					return false;
				}
			}
			if (bShowOnlyFunctionObjects)
			{
				if (!ObjectNode.Is<FFunctionObjectNode>())
				{
					return false;
				}
			}
			if (bShowOnlyPackageObjects)
			{
				if (!ObjectNode.Is<FPackageObjectNode>())
				{
					return false;
				}
			}
		}

		if (bHideObjectsWithZeroReferencingActors)
		{
			const uint32 NumReferences = ObjectNode.GetNumReferences();
			if (NumReferences == 0)
			{
				return false;
			}
		}

		if (bHideObjectsWithZeroEstimatedMemory)
		{
			const int64 EstimatedMemSize = ObjectNode.GetEstimatedMemorySize();
			if (EstimatedMemSize == 0)
			{
				return false;
			}
		}
		if (bHideObjectsWithLowEstimatedMemory)
		{
			const int64 EstimatedMemSize = ObjectNode.GetEstimatedMemorySize();
			if (EstimatedMemSize < 1024 * 1024)
			{
				return false;
			}
		}
		if (bHideObjectsWithLowImpact)
		{
			TOptional<double> EstimatedImpact = ObjectNode.GetEstimatedMemoryImpact();
			if (!EstimatedImpact.IsSet() || EstimatedImpact.GetValue() < 0.01)
			{
				return false;
			}
		}

		if (bHideObjectsWithZeroTotalEstimatedMemory)
		{
			const int64 EstimatedMemSize = ObjectNode.GetTotalEstimatedMemorySize();
			if (EstimatedMemSize == 0)
			{
				return false;
			}
		}
		if (bHideObjectsWithLowTotalEstimatedMemory)
		{
			const int64 EstimatedMemSize = ObjectNode.GetTotalEstimatedMemorySize();
			if (EstimatedMemSize < 1024 * 1024)
			{
				return false;
			}
		}
		if (bHideObjectsWithLowTotalImpact)
		{
			TOptional<double> EstimatedImpact = ObjectNode.GetTotalEstimatedMemoryImpact();
			if (!EstimatedImpact.IsSet() || EstimatedImpact.GetValue() < 0.01)
			{
				return false;
			}
		}

		if (bHidePackages)
		{
			TSharedPtr<FObjectNode> Outer = ObjectNode.GetOuter();
			if (!Outer.IsValid())
			{
				return false;
			}
		}
		if (bHideSubObjects)
		{
			TSharedPtr<FObjectNode> Outer = ObjectNode.GetOuter();
			if (Outer.IsValid() && Outer->GetOuter().IsValid())
			{
				return false;
			}
		}
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectTableTreeView::UpdateBannerText()
{
	if (bHideObjectsExternalToProject && FilteredNodesPtr && FilteredNodesPtr->Num() == 0 && TableRowNodes.Num() > 0)
	{
		TreeViewBannerText = LOCTEXT("OnlyExternalObjectsFound", "All objects have been filtered out. Uncheck 'Hide External Objects' in the toolbar.");
		return;
	}

	STableTreeView::UpdateBannerText();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FObjectNode> SObjectTableTreeView::GetObjectNode(uint32 ObjectId)
{
	return (ObjectId < uint32(ObjectNodes.Num())) ? ObjectNodes[ObjectId] :  nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectTableTreeView::SelectObjectNode(uint32 ObjectId)
{
	TSharedPtr<FObjectNode> NodePtr = GetObjectNode(ObjectId);
	if (NodePtr)
	{
		TreeView->SetSelection(NodePtr);
		TreeView->RequestScrollIntoView(NodePtr);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::ObjectProfiler

#undef LOCTEXT_NAMESPACE
#undef INSIGHTS_SHOW_FOOTER
