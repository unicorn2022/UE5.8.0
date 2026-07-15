// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SNasSHKBrowserPanel.h"

#include "Async/Async.h"
#include "DesktopPlatformModule.h"
#include "Dialog/SCustomDialog.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "Misc/App.h"
#include "NasSHKScanner.h"
#include "ShaderAuditSession.h"
#include "ShaderAuditTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "NasSHKBrowser"

// ============================================================================
// Row widget
// ============================================================================

class SNasBrowserRow : public SMultiColumnTableRow<TSharedPtr<FNasBrowserItem>>
{
public:
	SLATE_BEGIN_ARGS(SNasBrowserRow) {}
		SLATE_ARGUMENT(TSharedPtr<FNasBrowserItem>, Item)
		SLATE_EVENT(FSimpleDelegate, OnClearCache)
		SLATE_EVENT(FSimpleDelegate, OnLoad)
		SLATE_ARGUMENT(TWeakPtr<SNasSHKBrowserPanel>, OwnerPanel)
		SLATE_ARGUMENT(FString, GroupKey)
		SLATE_ARGUMENT(FString, FormatKey)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
	{
		Item = InArgs._Item;
		OnClearCache = InArgs._OnClearCache;
		OnLoad = InArgs._OnLoad;
		WeakPanel = InArgs._OwnerPanel;
		GroupKey = InArgs._GroupKey;
		FormatKey = InArgs._FormatKey;
		SMultiColumnTableRow::Construct(SMultiColumnTableRow::FArguments().Padding(FMargin(4.f, 2.f)), OwnerTable);
	}
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
	{
		if (!Item.IsValid())
		{
			return SNullWidget::NullWidget;
		}

		const bool bIsHeader = Item->Type == FNasBrowserItem::EType::GroupHeader;

		// ---- Branch column ----
		if (InColumnName == TEXT("Branch"))
		{
			if (bIsHeader && Item->Group.IsValid())
			{
				return SNew(STextBlock)
					.Text(FText::FromString(Item->Group->Branch))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10));
			}
			return SNullWidget::NullWidget;
		}

		// ---- CL column ----
		if (InColumnName == TEXT("CL"))
		{
			if (bIsHeader && Item->Group.IsValid())
			{
				return SNew(STextBlock)
					.Text(FText::AsNumber(Item->Group->CL))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10));
			}
			// Child rows: empty CL column
			return SNullWidget::NullWidget;
		}

		// ---- Formats column ----
		if (InColumnName == TEXT("Formats"))
		{
			if (bIsHeader && Item->Group.IsValid())
			{
				// Show format count (row click handles expand/collapse)
				const int32 NumFormats = Item->Group->GetNumFormats();
				return SNew(STextBlock)
					.Text(FText::Format(
						LOCTEXT("FormatCount", "   {0} format(s)"),
						FText::AsNumber(NumFormats)));
			}
			else if (const FSessionFileInventory* Inv = Item->GetInventory())
			{
				// Child row: indented format name + size info
				// Extract FormatName from the FormatKey ("TargetType|FormatName|Source")
				const FString BaseKey = FNasBuildGroup::GetBaseFormatKey(Item->FormatKey);
				FString DisplayFormatName = BaseKey;
				int32 PipeIdx;
				if (DisplayFormatName.FindChar(TEXT('|'), PipeIdx))
				{
					DisplayFormatName = DisplayFormatName.Mid(PipeIdx + 1);
				}
				FText DisplayText = FText::FromString(DisplayFormatName);

				return SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(20.f, 0.f, 4.f, 0.f)
					[
						SNew(STextBlock)
						.Text(DisplayText)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(6.f, 0.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Text_Lambda([GroupWeak = TWeakPtr<FNasBuildGroup>(Item->Group), FKey = FormatKey]() -> FText
						{
							auto Group = GroupWeak.Pin();
							const FSessionFileInventory* LiveInv = Group ? Group->Formats.Find(FKey) : nullptr;
							if (!LiveInv || LiveInv->SHKTotalBytes <= 0) { return FText::GetEmpty(); }

							const int32 SHKCount = LiveInv->SHKFiles.Num();
							const FString SHKSize = UE::ShaderAudit::Utils::FormatBytes(LiveInv->SHKTotalBytes);

							if (LiveInv->BytecodeTotalBytes > 0)
							{
								const FString BytecodeSize = UE::ShaderAudit::Utils::FormatBytes(LiveInv->BytecodeTotalBytes);
								return FText::FromString(FString::Printf(TEXT("%s shk (%d) + %s bytecode (%d)"),
									*SHKSize, SHKCount, *BytecodeSize, LiveInv->BytecodeFiles.Num()));
							}
							return FText::FromString(FString::Printf(TEXT("%s shk (%d)"), *SHKSize, SHKCount));
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Visibility(Inv->SHKTotalBytes > 0 ? EVisibility::Visible : EVisibility::Collapsed)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4.f, 0.f, 0.f, 0.f)
					[
						SNew(SCircularThrobber)
						.Radius(6.f)
						.NumPieces(4)
						.Period(0.75f)
						.Visibility_Lambda([PanelWeak = WeakPanel, GKey = GroupKey, FKey = FormatKey]() -> EVisibility
						{
							auto Panel = PanelWeak.Pin();
							return (Panel && Panel->IsFormatPending(GKey, FKey)) ? EVisibility::Visible : EVisibility::Collapsed;
						})
					];
			}
			return SNullWidget::NullWidget;
		}

		// ---- Date column ----
		if (InColumnName == TEXT("Date"))
		{
			if (bIsHeader && Item->Group.IsValid())
			{
				// Group header: most recent date across all formats
				return SNew(STextBlock)
					.Text_Lambda([GroupWeak = TWeakPtr<FNasBuildGroup>(Item->Group)]() -> FText
					{
						auto Group = GroupWeak.Pin();
						if (!Group) { return FText::GetEmpty(); }
						FDateTime Best;
						for (const TPair<FString, FSessionFileInventory>& Pair : Group->Formats)
						{
							const FDateTime T = Pair.Value.GetMostRecentTime();
							if (T > Best) { Best = T; }
						}
						if (Best == FDateTime()) { return FText::GetEmpty(); }
						return FText::AsDate(Best);
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10));
			}
			else if (const FSessionFileInventory* Inv = Item->GetInventory())
			{
				return SNew(STextBlock)
					.Text_Lambda([GroupWeak = TWeakPtr<FNasBuildGroup>(Item->Group), FKey = FormatKey]() -> FText
					{
						auto Group = GroupWeak.Pin();
						const FSessionFileInventory* LiveInv = Group ? Group->Formats.Find(FKey) : nullptr;
						if (!LiveInv) { return FText::GetEmpty(); }
						const FDateTime T = LiveInv->GetMostRecentTime();
						if (T == FDateTime()) { return FText::GetEmpty(); }
						return FText::AsDate(T);
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground());
			}
			return SNullWidget::NullWidget;
		}

		// ---- Status column ----
		if (InColumnName == TEXT("Status"))
		{
			if (bIsHeader && Item->Group.IsValid())
			{
				// Group header: overall status + clear cache button
				const int32 NumCached = Item->Group->GetNumCachedFormats();
				const int32 NumFormats = Item->Group->GetNumFormats();
				const bool bHasCached = NumCached > 0;
				const bool bAllCached = (NumCached == NumFormats);
				FText StatusText;
				FLinearColor StatusColor;
				if (bAllCached)
				{
					StatusText = LOCTEXT("StatusCached", "Cached");
					StatusColor = FLinearColor::Green;
				}
				else if (bHasCached)
				{
					StatusText = LOCTEXT("StatusPartial", "Partial");
					StatusColor = FLinearColor(0.8f, 0.7f, 0.2f);
				}
				else
				{
					StatusText = LOCTEXT("StatusNAS", "NAS");
					StatusColor = FSlateColor::UseForeground().GetSpecifiedColor();
				}
				return SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(StatusText)
						.ColorAndOpacity(FSlateColor(StatusColor))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton)
						.ButtonStyle(FCoreStyle::Get(), "NoBorder")
						.ContentPadding(0)
						.Visibility(bHasCached ? EVisibility::Visible : EVisibility::Collapsed)
						.ToolTipText(LOCTEXT("ClearCLTip", "Remove cached files for this CL"))
						.OnClicked_Lambda([this]() -> FReply
						{
							OnClearCache.ExecuteIfBound();
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ClearCLBtn", "X"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
							.ColorAndOpacity(FLinearColor(0.8f, 0.2f, 0.2f))
						]
					];
			}
			else if (Item->GetInventory())
			{
				const bool bEntryCached = Item->IsCached();
				// Format entry: Load button + cached/NAS label + clear cache
				return SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.Text(LOCTEXT("LoadEntryBtn", "Load"))
						.Visibility_Lambda([PanelWeak = WeakPanel, GKey = GroupKey, FKey = FormatKey]() -> EVisibility
						{
							auto Panel = PanelWeak.Pin();
							return (Panel && !Panel->IsFormatPending(GKey, FKey)) ? EVisibility::Visible : EVisibility::Hidden;
						})
						.OnClicked_Lambda([this]() -> FReply
						{
							OnLoad.ExecuteIfBound();
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(6.f, 0.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Text(bEntryCached ? LOCTEXT("EntryCached", "Cached") : LOCTEXT("EntryNAS", "NAS"))
						.ColorAndOpacity(bEntryCached ? FLinearColor::Green : FSlateColor::UseForeground())
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton)
						.ButtonStyle(FCoreStyle::Get(), "NoBorder")
						.ContentPadding(0)
						.Visibility(bEntryCached ? EVisibility::Visible : EVisibility::Collapsed)
						.ToolTipText(LOCTEXT("ClearEntryTip", "Remove this cached file"))
						.OnClicked_Lambda([this]() -> FReply
						{
							OnClearCache.ExecuteIfBound();
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ClearEntryBtn", "X"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
							.ColorAndOpacity(FLinearColor(0.8f, 0.2f, 0.2f))
						]
					];
			}
			return SNullWidget::NullWidget;
		}

		return SNullWidget::NullWidget;
	}

private:
	TSharedPtr<FNasBrowserItem> Item;
	FSimpleDelegate OnClearCache;
	FSimpleDelegate OnLoad;
	TWeakPtr<SNasSHKBrowserPanel> WeakPanel;
	FString GroupKey;
	FString FormatKey;
};

SNasSHKBrowserPanel::~SNasSHKBrowserPanel()
{
	if (ScanCancelToken)
	{
		ScanCancelToken->store(true, std::memory_order_relaxed);
	}
}

// ============================================================================
// SNasSHKBrowserPanel
// ============================================================================

void SNasSHKBrowserPanel::Construct(const FArguments& InArgs)
{
	OnSessionsLoaded = InArgs._OnSessionsLoaded;

	NasDir = FNasSHKScanner::GetNasLocationFromConfig();
	CacheDir = FNasSHKScanner::GetDefaultCacheDir();
	CurrentBranchName = FApp::GetBranchName();
	BuildRoot = FNasSHKScanner::GetBuildRootFromConfig();

	// Detect misconfiguration: no project set means config won't have project-specific settings
	const bool bHasProject = FPaths::IsProjectFilePathSet() && !FPaths::GetProjectFilePath().IsEmpty();

	FString NasDisplayText;
	FLinearColor NasTextColor;
	if (!bHasProject)
	{
		NasDisplayText = TEXT("No project loaded -- launch with a .uproject path to read SHKFilesLocation from config");
		NasTextColor = FLinearColor(0.9f, 0.3f, 0.1f);
	}
	else if (NasDir.IsEmpty())
	{
		NasDisplayText = FString::Printf(TEXT("Not configured -- add SHKFilesLocation to [/Script/Engine.ShaderCompilerStats] in %sConfig/DefaultGame.ini"),
			*FPaths::ProjectDir());
		NasTextColor = FLinearColor(0.6f, 0.4f, 0.2f);
	}
	else
	{
		NasDisplayText = NasDir;
		NasTextColor = FLinearColor(0.5f, 0.5f, 0.5f);
	}

	// BuildRoot configuration
	FString BuildRootDisplayText;
	FLinearColor BuildRootTextColor;
	if (!bHasProject)
	{
		BuildRootDisplayText = TEXT("(no project)");
		BuildRootTextColor = FLinearColor(0.9f, 0.3f, 0.1f);
	}
	else if (BuildRoot.IsEmpty())
	{
		BuildRootDisplayText = TEXT("Not configured -- add BuildRoot to [NASBrowser] in Plugins/ShaderAudit/Config/ShaderAudit.ini (required for bytecode fetch)");
		BuildRootTextColor = FLinearColor(0.9f, 0.6f, 0.2f);
	}
	else
	{
		BuildRootDisplayText = BuildRoot;
		BuildRootTextColor = FLinearColor(0.5f, 0.5f, 0.5f);
	}

	// Header row for the list view
	TSharedPtr<SHeaderRow> HeaderRow = SNew(SHeaderRow)
		+ SHeaderRow::Column(TEXT("Branch"))
			.DefaultLabel(LOCTEXT("ColBranch", "Branch"))
			.FillWidth(0.15f)
		+ SHeaderRow::Column(TEXT("CL"))
			.DefaultLabel(LOCTEXT("ColCL", "CL"))
			.FillWidth(0.07f)
		+ SHeaderRow::Column(TEXT("Date"))
			.DefaultLabel(LOCTEXT("ColDate", "Date"))
			.FillWidth(0.07f)
		+ SHeaderRow::Column(TEXT("Formats"))
			.DefaultLabel(LOCTEXT("ColFormats", "Formats"))
			.FillWidth(0.51f)
		+ SHeaderRow::Column(TEXT("Status"))
			.DefaultLabel(LOCTEXT("ColStatus", "Status"))
			.FillWidth(0.20f);

	ChildSlot
	[
		SNew(SVerticalBox)

		// --- Branch + path info header ---
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.f, 8.f, 12.f, 2.f)
		[
			SNew(STextBlock)
			.Text(FText::Format(
				LOCTEXT("BranchHeader", "Branch: {0}   Project: {1}"),
				FText::FromString(CurrentBranchName.IsEmpty() ? TEXT("(unknown)") : CurrentBranchName),
				FText::FromString(!FPaths::IsProjectFilePathSet() ? TEXT("(unknown)") : FPaths::GetBaseFilename(FPaths::GetProjectFilePath()))))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.f, 2.f, 12.f, 2.f)
		[
			SNew(STextBlock)
			.Text(FText::Format(
				LOCTEXT("LocalPath", "Local: {0}"),
				FText::FromString(CacheDir)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.f, 0.f, 12.f, 4.f)
		[
			SNew(STextBlock)
			.Text(FText::Format(
				LOCTEXT("NasPath", "NAS: {0}"),
				FText::FromString(NasDisplayText)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor(NasTextColor))
			.AutoWrapText(true)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.f, 0.f, 12.f, 4.f)
		[
			SNew(STextBlock)
			.Text(FText::Format(
				LOCTEXT("BuildRootPath", "Build Root: {0}"),
				FText::FromString(BuildRootDisplayText)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor(BuildRootTextColor))
			.AutoWrapText(true)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.f, 0.f)
		[
			SNew(SSeparator)
		]

		// --- Branch filter + buttons ---
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.f, 6.f, 12.f, 4.f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("BranchLabel", "Branch:"))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(BranchCombo, SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&BranchOptions)
				.OnSelectionChanged(this, &SNasSHKBrowserPanel::OnBranchSelected)
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
				{
					return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")));
				})
				.Content()
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return FText::FromString(SelectedBranch.IsValid() ? *SelectedBranch : TEXT("(All)"));
					})
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("RefreshBtn", "Refresh"))
				.OnClicked_Lambda([this]() -> FReply
				{
					ResetScanState();
					StartScan();
					return FReply::Handled();
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ClearAllBtn", "Clear Cache"))
				.ToolTipText(LOCTEXT("ClearAllTip", "Delete all locally cached SHK files"))
				.OnClicked_Lambda([this]() -> FReply
				{
					OnClearAllCache();
					return FReply::Handled();
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("BrowseBtn", "Browse..."))
				.ToolTipText(LOCTEXT("BrowseTip", "Browse for local SHK files manually"))
				.OnClicked_Lambda([this]() -> FReply
				{
					OnBrowseLocal();
					return FReply::Handled();
				})
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.f, 0.f)
		[
			SNew(SSeparator)
		]

		// --- List view ---
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(4.f)
		[
			SAssignNew(ListView, SListView<TSharedPtr<FNasBrowserItem>>)
			.ListItemsSource(&DisplayItems)
			.OnGenerateRow(this, &SNasSHKBrowserPanel::OnGenerateRow)
			.OnSelectionChanged_Lambda([this](TSharedPtr<FNasBrowserItem> ClickedItem, ESelectInfo::Type)
			{
				if (ClickedItem.IsValid())
				{
					if (ClickedItem->Type == FNasBrowserItem::EType::GroupHeader && ClickedItem->Group.IsValid())
					{
						const FString GroupKey = ClickedItem->Group->GetGroupKey();
						ToggleGroupExpanded(GroupKey);
					}
				}
				// Clear selection so the same row can be clicked again
				if (ListView.IsValid())
				{
					ListView->ClearSelection();
				}
			})
			.SelectionMode(ESelectionMode::Single)
			.HeaderRow(HeaderRow.ToSharedRef())
		]

		// --- Progress / status ---
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.f, 4.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(NasStatusText, STextBlock)
					.Text(LOCTEXT("ScanIdle", "Press Refresh to scan for available builds."))
					.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.ButtonStyle(FCoreStyle::Get(), "NoBorder")
					.ContentPadding(FMargin(4.f, 2.f))
					.Visibility_Lambda([this]() { return bNasScanComplete ? EVisibility::Collapsed : EVisibility::Visible; })
					.OnClicked_Lambda([this]() -> FReply
					{
						CancelScan();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT("CancelScanBtn", "Cancel"))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity(FLinearColor(0.8f, 0.2f, 0.2f))
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 2.f, 0.f, 0.f)
			[
				SAssignNew(LocalProgressBar, SProgressBar)
				.Visibility(EVisibility::Collapsed)
				.FillColorAndOpacity(FLinearColor(0.2f, 0.8f, 0.3f))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 2.f, 0.f, 0.f)
			[
				SAssignNew(NasProgressBar, SProgressBar)
				.Visibility(EVisibility::Collapsed)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.f, 0.f)
		[
			SNew(SSeparator)
		]

		// --- Bottom buttons ---
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(12.f, 6.f, 12.f, 10.f)
		[
			SNew(SHorizontalBox)
		]
	];

	// Auto-start local cache scan on next tick (SharedThis is unsafe during Construct)
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda(
		[this](double /*InCurrentTime*/, float /*InDeltaTime*/) -> EActiveTimerReturnType
		{
			StartScan(EScanFlags::Local);
			return EActiveTimerReturnType::Stop;
		}));
}

// ============================================================================
// Scanning
// ============================================================================

void SNasSHKBrowserPanel::ResetScanState()
{
	++ScanGeneration[ScanFlagIndex(EScanFlags::Local)];
	++ScanGeneration[ScanFlagIndex(EScanFlags::NAS)];
	Groups.Empty();
	Branches.Empty();
	DisplayItems.Empty();
	ExpandedGroups.Empty();
	FormatStates.Empty();

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

void SNasSHKBrowserPanel::StartScan(EScanFlags Flags)
{
	const bool bScanLocal = EnumHasAnyFlags(Flags, EScanFlags::Local);
	const bool bScanNas = EnumHasAnyFlags(Flags, EScanFlags::NAS);

	bNasScanComplete = !bScanNas;
	if (bScanLocal) { ++ScanGeneration[ScanFlagIndex(EScanFlags::Local)]; }
	if (bScanNas)   { ++ScanGeneration[ScanFlagIndex(EScanFlags::NAS)]; }

	// Cancel any in-flight scan and create a fresh token
	if (ScanCancelToken)
	{
		ScanCancelToken->store(true, std::memory_order_relaxed);
	}
	ScanCancelToken = MakeShared<std::atomic<bool>>(false);
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}

	if (NasStatusText.IsValid())
	{
		NasStatusText->SetText(LOCTEXT("Scanning", "Scanning..."));
	}

	// Show appropriate progress bar(s)
	if (bScanLocal && LocalProgressBar.IsValid())
	{
		LocalProgressBar->SetVisibility(EVisibility::Visible);
		LocalProgressBar->SetPercent(TOptional<float>()); // indeterminate
	}

	if (bScanNas && NasProgressBar.IsValid())
	{
		NasProgressBar->SetVisibility(EVisibility::Visible);
		NasProgressBar->SetPercent(TOptional<float>()); // indeterminate
	}

	TWeakPtr<SNasSHKBrowserPanel> WeakThis(SharedThis(this));
	const uint32 LocalGen = ScanGeneration[ScanFlagIndex(EScanFlags::Local)];
	const uint32 NasGen = ScanGeneration[ScanFlagIndex(EScanFlags::NAS)];

	// Pass empty strings for sources we don't want to scan --
	// ScanAsync handles empty NasDir (fires OnNasComplete immediately)
	// and empty CacheDir (skips local scan) gracefully.
	FNasSHKScanner::ScanAsync(
		bScanNas ? NasDir : FString(),
		bScanLocal ? CacheDir : FString(),
		[WeakThis, LocalGen](FNasScanResult Result)
		{
			if (TSharedPtr<SNasSHKBrowserPanel> This = WeakThis.Pin())
			{
				if (This->ScanGeneration[ScanFlagIndex(EScanFlags::Local)] == LocalGen)
				{
					This->OnLocalScanComplete(MoveTemp(Result));
				}
			}
		},
		bScanNas ? TFunction<void(FNasScanResult)>([WeakThis, NasGen](FNasScanResult Result)
		{
			if (TSharedPtr<SNasSHKBrowserPanel> This = WeakThis.Pin())
			{
				if (This->ScanGeneration[ScanFlagIndex(EScanFlags::NAS)] == NasGen)
				{
					This->OnNasScanProgress(MoveTemp(Result));
				}
			}
		}) : nullptr,
		bScanNas ? TFunction<void(FNasScanResult)>([WeakThis, NasGen](FNasScanResult Result)
		{
			if (TSharedPtr<SNasSHKBrowserPanel> This = WeakThis.Pin())
			{
				if (This->ScanGeneration[ScanFlagIndex(EScanFlags::NAS)] == NasGen)
				{
					This->OnNasScanComplete(MoveTemp(Result));
				}
			}
		}) : nullptr,
		ScanCancelToken);
}

void SNasSHKBrowserPanel::CancelScan()
{
	if (ScanCancelToken)
	{
		ScanCancelToken->store(true, std::memory_order_relaxed);
	}

	if (LocalProgressBar.IsValid())
	{
		LocalProgressBar->SetVisibility(EVisibility::Collapsed);
	}

	if (NasProgressBar.IsValid())
	{
		NasProgressBar->SetVisibility(EVisibility::Collapsed);
	}

	if (NasStatusText.IsValid())
	{
		NasStatusText->SetText(LOCTEXT("ScanCancelled", "Scan cancelled"));
	}

	bNasScanComplete = true;
}

void SNasSHKBrowserPanel::RefreshLocalCache(const FString& GroupKey)
{
	// Remove pending keys and cached format entries for the affected scope
	if (GroupKey.IsEmpty())
	{
		// All groups: remove cached format entries and their FormatStates.
		// Preserve NAS FormatStates so in-flight Gather tasks aren't accepted as stale.
		for (TPair<FString, TSharedPtr<FNasBuildGroup>>& GroupPair : Groups)
		{
			for (auto It = GroupPair.Value->Formats.CreateIterator(); It; ++It)
			{
				if (FNasBuildGroup::IsFormatCached(It.Key()))
				{
					FormatStates.Remove(MakePendingKey(GroupPair.Key, It.Key()));
					It.RemoveCurrent();
				}
			}
		}
	}
	else
	{
		// Single group: remove cached format entries and their FormatStates.
		// Preserve NAS FormatStates so in-flight Gather tasks keep their generation.
		if (TSharedPtr<FNasBuildGroup>* GroupPtr = Groups.Find(GroupKey))
		{
			for (auto It = (*GroupPtr)->Formats.CreateIterator(); It; ++It)
			{
				if (FNasBuildGroup::IsFormatCached(It.Key()))
				{
					FormatStates.Remove(MakePendingKey(GroupKey, It.Key()));
					It.RemoveCurrent();
				}
			}
		}
	}

	// Immediately rebuild display with NAS-only state while local scan runs
	RebuildFilteredItems();
	// Kick async local-only scan; generation check in StartScan's callback
	// ensures stale results are discarded.
	StartScan(EScanFlags::Local);
}

void SNasSHKBrowserPanel::UpdateBranchList()
{
	BranchOptions.Empty();
	TSharedPtr<FString> AllOption = MakeShared<FString>(TEXT("(All)"));
	BranchOptions.Add(AllOption);

	for (const FString& B : Branches)
	{
		TSharedPtr<FString> Opt = MakeShared<FString>(B);
		BranchOptions.Add(Opt);
	}

	// If nothing selected yet, default to (All)
	if (!SelectedBranch.IsValid())
	{
		SelectedBranch = AllOption;
	}
	else
	{
		// Preserve current selection across rebuilds (pointer changed, match by value)
		bool bFound = false;
		for (const TSharedPtr<FString>& Opt : BranchOptions)
		{
			if (*Opt == *SelectedBranch)
			{
				SelectedBranch = Opt;
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			SelectedBranch = AllOption;
		}
	}

	if (BranchCombo.IsValid())
	{
		BranchCombo->SetSelectedItem(SelectedBranch);
		BranchCombo->RefreshOptions();
	}
}

void SNasSHKBrowserPanel::OnLocalScanComplete(FNasScanResult Result)
{
	if (LocalProgressBar.IsValid())
	{
		LocalProgressBar->SetVisibility(EVisibility::Collapsed);
	}

	MergeScanResult(Result);
	UpdateBranchList();
	RebuildFilteredItems();
	UpdateStatusText();

	// Re-fire bytecode discovery for all expanded groups
	// (ToggleGroupExpanded skips formats that already have bytecode)
	TArray<FString> ExpandedKeys = ExpandedGroups.Array();
	for (const FString& Key : ExpandedKeys)
	{
		ExpandedGroups.Remove(Key);
		ToggleGroupExpanded(Key);
	}
}

void SNasSHKBrowserPanel::OnNasScanProgress(FNasScanResult PartialResult)
{
	MergeScanResult(PartialResult);
	UpdateBranchList();
	RebuildFilteredItems();
	UpdateStatusText();
}

void SNasSHKBrowserPanel::OnNasScanComplete(FNasScanResult Result)
{
	bNasScanComplete = true;

	if (NasProgressBar.IsValid())
	{
		NasProgressBar->SetVisibility(EVisibility::Collapsed);
	}

	MergeScanResult(Result);
	UpdateBranchList();
	RebuildFilteredItems();
	UpdateStatusText();
}

void SNasSHKBrowserPanel::UpdateStatusText()
{
	if (!NasStatusText.IsValid())
	{
		return;
	}

	// Count cached vs remote builds
	int32 NumCached = 0;
	int32 NumRemote = 0;
	for (const TPair<FString, TSharedPtr<FNasBuildGroup>>& Pair : Groups)
	{
		if (Pair.Value->GetNumCachedFormats() > 0)
		{
			++NumCached;
		}
		else
		{
			++NumRemote;
		}
	}

	if (!bNasScanComplete)
	{
		// NAS scan still in progress
		NasStatusText->SetText(FText::Format(
			LOCTEXT("StatusScanning", "{0} cached, {1} remote (scanning NAS...)"),
			FText::AsNumber(NumCached),
			FText::AsNumber(NumRemote)));
	}
	else
	{
		// All scans complete
		NasStatusText->SetText(FText::Format(
			LOCTEXT("StatusDone", "{0} cached, {1} remote"),
			FText::AsNumber(NumCached),
			FText::AsNumber(NumRemote)));
	}
}

bool SNasSHKBrowserPanel::TryMergeInventory(
	const FString& GroupKey, const FString& FormatKey,
	FSessionFileInventory&& Inventory, int32 Generation)
{
	TSharedPtr<FNasBuildGroup>* GroupPtr = Groups.Find(GroupKey);
	if (!GroupPtr || !GroupPtr->IsValid())
	{
		return false;
	}

	const FString StateKey = MakePendingKey(GroupKey, FormatKey);
	const int32 CurrentGen = FMath::Abs(FormatStates.FindRef(StateKey));

	if (Generation < CurrentGen)
	{
		// Stale data — a higher-generation inventory is already stored.
		return false;
	}

	(*GroupPtr)->Formats.FindOrAdd(FormatKey) = MoveTemp(Inventory);
	FormatStates.FindOrAdd(StateKey) = Generation;
	return true;
}

void SNasSHKBrowserPanel::MergeScanResult(const FNasScanResult& InResult)
{
	// Format keys include the source suffix (|Local or |NAS), so NAS and Local
	// entries for the same format never collide. Groups merge by Branch|CL.
	for (const FNasBuildGroup& InGroup : InResult.Groups)
	{
		const FString Key = InGroup.GetGroupKey();
		TSharedPtr<FNasBuildGroup>& ExistingPtr = Groups.FindOrAdd(Key);

		if (!ExistingPtr)
		{
			ExistingPtr = MakeShared<FNasBuildGroup>(InGroup);
		}
		else
		{
			for (const TPair<FString, FSessionFileInventory>& Pair : InGroup.Formats)
			{
				if (!ExistingPtr->Formats.Contains(Pair.Key))
				{
					ExistingPtr->Formats.Add(Pair.Key, Pair.Value);
				}
				else
				{
					// Scanner data is always generation 0.
					// Enriched inventories (gen >= 1 from Gather) always win.
					TryMergeInventory(Key, Pair.Key, FSessionFileInventory(Pair.Value), /*Generation=*/ 0);
				}
			}
		}
	}

	// Merge branches
	TSet<FString> BranchSet(Branches);
	for (const FString& B : InResult.Branches)
	{
		BranchSet.Add(B);
	}
	Branches = BranchSet.Array();
	Branches.Sort();
}

void SNasSHKBrowserPanel::RebuildFilteredItems()
{
	DisplayItems.Empty();

	const bool bFilterAll = !SelectedBranch.IsValid() || *SelectedBranch == TEXT("(All)");

	// Sort groups by CL descending
	TArray<TSharedPtr<FNasBuildGroup>> SortedGroups;
	Groups.GenerateValueArray(SortedGroups);
	SortedGroups.Sort([](const TSharedPtr<FNasBuildGroup>& A, const TSharedPtr<FNasBuildGroup>& B)
	{
		return A->CL > B->CL;
	});

	for (const TSharedPtr<FNasBuildGroup>& GroupPtr : SortedGroups)
	{
		if (bFilterAll || GroupPtr->Branch == *SelectedBranch)
		{
			const FString GroupKey = GroupPtr->GetGroupKey();
			DisplayItems.Add(FNasBrowserItem::MakeGroupHeader(GroupPtr));

			// If expanded, show one row per base format, preferring Local over NAS.
			if (ExpandedGroups.Contains(GroupKey))
			{
				const TMap<FString, FString> BestFormatKeys = GroupPtr->ResolveBestFormats();

				// Sort by base key (TargetType|FormatName) for stable alphabetical order
				TArray<TPair<FString, FString>> SortedFormats;
				for (const TPair<FString, FString>& BestPair : BestFormatKeys)
				{
					SortedFormats.Emplace(BestPair.Key, BestPair.Value);
				}
				SortedFormats.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B)
				{
					return A.Key < B.Key;
				});

				for (const TPair<FString, FString>& SortedPair : SortedFormats)
				{
					DisplayItems.Add(FNasBrowserItem::MakeFormatEntry(GroupPtr, SortedPair.Value));
				}
			}
		}
	}

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

// ============================================================================
// Expand / collapse
// ============================================================================

void SNasSHKBrowserPanel::ToggleGroupExpanded(const FString& GroupKey)
{
	if (ExpandedGroups.Contains(GroupKey))
	{
		ExpandedGroups.Remove(GroupKey);
	}
	else
	{
		ExpandedGroups.Add(GroupKey);
	}
	RebuildFilteredItems();

	// Fire async bytecode discovery per format if not already done (after RebuildFilteredItems so DisplayItems are current)
	if (!BuildRoot.IsEmpty() && ExpandedGroups.Contains(GroupKey))
	{
		TSharedPtr<FNasBuildGroup>* FoundGroupPtr = Groups.Find(GroupKey);

		if (FoundGroupPtr && FoundGroupPtr->IsValid())
		{
			TWeakPtr<SNasSHKBrowserPanel> WeakThis(SharedThis(this));
			const TMap<FString, FString> BestFormatKeys = (*FoundGroupPtr)->ResolveBestFormats();

			for (const TPair<FString, FString>& BestPair : BestFormatKeys)
			{
				const FString& FormatKey = BestPair.Value;
				const FSessionFileInventory& Inv = (*FoundGroupPtr)->Formats.FindChecked(FormatKey);

				// Skip if bytecode already attempted (>= 0) or currently pending/enriched
				const FString StateKey = MakePendingKey(GroupKey, FormatKey);
				if (Inv.BytecodeTotalBytes >= 0)
				{
					continue;
				}

				// Mark pending: negative, abs = generation (one higher than current)
				const int32 GatherGen = FMath::Abs(FormatStates.FindRef(StateKey)) + 1;
				FormatStates.Add(StateKey, -GatherGen);

				// Pass pre-computed file stats to avoid re-statting on NAS
				TSharedRef<TMap<FString, FFileStatData>> PrecomputedStats = MakeShared<TMap<FString, FFileStatData>>(Inv.FileStats);

				Async(EAsyncExecution::TaskGraph, [SiblingPaths = Inv.SHKFiles, PrecomputedStats, WeakThis, FormatKey, GroupKey, GatherGen]()
				{
					FSessionFileInventory GatheredInv = FSessionFileInventory::Gather(SiblingPaths, /*bShowProgress=*/ false, &(*PrecomputedStats));

					Async(EAsyncExecution::TaskGraphMainThread, [WeakThis, FormatKey, GroupKey, GatherGen, GatheredInv = MoveTemp(GatheredInv)]() mutable
					{
						if (TSharedPtr<SNasSHKBrowserPanel> This = WeakThis.Pin())
						{
							if (This->TryMergeInventory(GroupKey, FormatKey, MoveTemp(GatheredInv), GatherGen))
							{
								This->RebuildFilteredItems();
							}
						}
					});
				});
			}
		}
	}
}


// ============================================================================
// UI callbacks
// ============================================================================

TSharedRef<ITableRow> SNasSHKBrowserPanel::OnGenerateRow(
	TSharedPtr<FNasBrowserItem> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	FSimpleDelegate ClearDelegate;

	if (Item.IsValid() && Item->Group.IsValid())
	{
		const int32 CL = Item->Group->CL;
		const FString Branch = Item->Group->Branch;
		TWeakPtr<SNasSHKBrowserPanel> WeakThis(SharedThis(this));

		if (Item->Type == FNasBrowserItem::EType::FormatEntry)
		{
			// Per-format clear: delete only this TargetType's cached directory
			// Extract TargetType from FormatKey ("TargetType|FormatName")
			FString TargetType = Item->FormatKey;
			int32 PipeIdx;
			if (TargetType.FindChar(TEXT('|'), PipeIdx))
			{
				TargetType.LeftInline(PipeIdx);
			}
			ClearDelegate = FSimpleDelegate::CreateLambda([WeakThis, Branch, CL, TargetType]()
			{
				if (TSharedPtr<SNasSHKBrowserPanel> This = WeakThis.Pin())
				{
					This->OnClearCachedEntry(Branch, CL, TargetType);
				}
			});
		}
		else
		{
			// Group header clear: delete entire CL directory (all platforms)
			ClearDelegate = FSimpleDelegate::CreateLambda([WeakThis, Branch, CL]()
			{
				if (TSharedPtr<SNasSHKBrowserPanel> This = WeakThis.Pin())
				{
					This->OnClearCachedEntry(Branch, CL, FString());
				}
			});
		}
	}

	// Compute keys for the row
	FString GroupKey;
	FString FormatKey;

	if (Item.IsValid() && Item->Type == FNasBrowserItem::EType::FormatEntry && Item->Group.IsValid())
	{
		GroupKey = Item->Group->GetGroupKey();
		FormatKey = Item->FormatKey;
	}

	// Build load delegate for format entries
	FSimpleDelegate LoadDelegate;
	if (Item.IsValid() && Item->Type == FNasBrowserItem::EType::FormatEntry && Item->Group.IsValid())
	{
		TWeakPtr<SNasSHKBrowserPanel> WeakThis(SharedThis(this));

		LoadDelegate = FSimpleDelegate::CreateLambda([WeakThis, GroupKey, FormatKey]()
		{
			if (TSharedPtr<SNasSHKBrowserPanel> This = WeakThis.Pin())
			{
				This->OnLoadEntry(GroupKey, FormatKey);
			}
		});
	}

	return SNew(SNasBrowserRow, OwnerTable)
		.Item(Item)
		.OnClearCache(ClearDelegate)
		.OnLoad(LoadDelegate)
		.OwnerPanel(SharedThis(this))
		.GroupKey(GroupKey)
		.FormatKey(FormatKey);
}

void SNasSHKBrowserPanel::OnBranchSelected(TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo)
{
	SelectedBranch = Item;
	RebuildFilteredItems();
}

void SNasSHKBrowserPanel::OnLoadEntry(
	const FString& GroupKey,
	const FString& FormatKey)
{
	// Direct lookup from the Groups map
	const FSessionFileInventory* Inv = nullptr;
	if (const TSharedPtr<FNasBuildGroup>* GroupPtr = Groups.Find(GroupKey))
	{
		Inv = (*GroupPtr)->Formats.Find(FormatKey);
	}

	if (Inv && Inv->SHKFiles.Num() > 0)
	{
		TArray<TSharedPtr<FShaderAuditSession>> NewSessions = FShaderAuditSession::LoadFromInventory(*Inv);
		FShaderAuditSession::GetSessions().Append(NewSessions);
		OnSessionsLoaded.ExecuteIfBound();
	}

	// Refresh this group's cache state (loading may have cached new files)
	RefreshLocalCache(GroupKey);
}

void SNasSHKBrowserPanel::ConfirmAndClearCache(const FText& Message, TFunction<void()> OnConfirmed, const FString& GroupKeyToRefresh)
{
	const TSharedRef<SCustomDialog> Dialog = SNew(SCustomDialog)
		.Title(LOCTEXT("ClearCacheTitle", "Clear Cache"))
		.Content()
		[
			SNew(STextBlock)
			.Text(Message)
			.AutoWrapText(true)
		]
		.Buttons({
			SCustomDialog::FButton(LOCTEXT("ClearBtn", "Clear")),
			SCustomDialog::FButton(LOCTEXT("CancelBtn2", "Cancel"))
		});

	if (Dialog->ShowModal() == 0)
	{
		OnConfirmed();
		RefreshLocalCache(GroupKeyToRefresh);
	}
}

void SNasSHKBrowserPanel::OnClearAllCache()
{
	ConfirmAndClearCache(
		FText::Format(LOCTEXT("ClearAllMsg", "Delete all cached SHK files in:\n{0}"),
			FText::FromString(CacheDir)),
		[this]() { ClearCachedSessionDir(CacheDir); });
}

void SNasSHKBrowserPanel::OnClearCachedEntry(const FString& Branch, int32 CL, const FString& TargetType)
{
	FString DirToDelete;
	FText Message;

	if (TargetType.IsEmpty())
	{
		// Group-level: delete entire CL directory (all platforms)
		DirToDelete = FPaths::Combine(CacheDir, FString::Printf(TEXT("%s-CL-%d"), *Branch, CL));
		Message = FText::Format(LOCTEXT("ClearCLMsg", "Delete all cached files for CL {0}?"),
			FText::AsNumber(CL));
	}
	else
	{
		// Entry-level: delete only the specific TargetType subdirectory
		DirToDelete = FPaths::Combine(CacheDir, FString::Printf(TEXT("%s-CL-%d"), *Branch, CL), TargetType);
		Message = FText::Format(LOCTEXT("ClearEntryMsg", "Delete cached files for CL {0} ({1})?"),
			FText::AsNumber(CL), FText::FromString(TargetType));
	}

	const FString GroupKey = FNasBuildGroup::MakeGroupKey(Branch, CL);
	ConfirmAndClearCache(Message,
		[DirToDelete]() { ClearCachedSessionDir(DirToDelete); },
		GroupKey);
}

void SNasSHKBrowserPanel::OnBrowseLocal()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return;
	}

	const void* ParentHandle = nullptr;

	TArray<FString> OutFiles;
	DesktopPlatform->OpenFileDialog(
		ParentHandle,
		TEXT("Select SHK File"),
		TEXT(""),
		TEXT(""),
		TEXT("SHK Files (*.shk)|*.shk"),
		EFileDialogFlags::Multiple,
		OutFiles);

	if (OutFiles.Num() > 0)
	{
		// Expand to include all SHK siblings (same session name)
		TArray<FString> AllSiblings = FSessionFileInventory::FindSHKSiblings(OutFiles[0], /*bShowProgress=*/ true);
		FSessionFileInventory Inventory = FSessionFileInventory::Gather(AllSiblings);
		TArray<TSharedPtr<FShaderAuditSession>> NewSessions = FShaderAuditSession::LoadFromInventory(Inventory);
		FShaderAuditSession::GetSessions().Append(NewSessions);
		OnSessionsLoaded.ExecuteIfBound();
	}
}

#undef LOCTEXT_NAMESPACE
