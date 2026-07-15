// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SSHKEntryListWidget.h"

#include "ShaderAuditCore.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "ShaderAuditSession.h"
#include "ShaderCodeLibrary.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSuggestionTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "SSHKEntryListWidget"

// ============================================================================
// Row widget -- one per visible entry, created on demand by SListView
// ============================================================================

class SSHKEntryRow : public SMultiColumnTableRow<TSharedPtr<int32>>
{
public:
	SLATE_BEGIN_ARGS(SSHKEntryRow) {}
		SLATE_ARGUMENT(int32, EntryIndex)
		SLATE_ARGUMENT(TSharedPtr<FShaderAuditSession>, Session)
		SLATE_ARGUMENT(const TArray<FName>*, ColumnIds)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
	{
		EntryIndex = InArgs._EntryIndex;
		Session = InArgs._Session;
		ColumnIds = InArgs._ColumnIds;

		// Pre-split the entry string into columns once per row construction
		if (Session.IsValid() && Session->StableShaderKeyAndValueArray.IsValidIndex(EntryIndex))
		{
			const FString Line = Session->StableShaderKeyAndValueArray[EntryIndex].ToString();
			Line.ParseIntoArray(CachedColumns, TEXT(","), false);
		}

		SMultiColumnTableRow::Construct(SMultiColumnTableRow::FArguments().Padding(FMargin(2.f, 1.f)), OwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
	{
		check(ColumnIds);
		const int32 ColIdx = ColumnIds->IndexOfByKey(InColumnName);
		const FString& CellText = CachedColumns.IsValidIndex(ColIdx) ? CachedColumns[ColIdx] : FString();

		return SNew(STextBlock)
			.Text(FText::FromString(CellText))
			.Font(FCoreStyle::Get().GetFontStyle("SmallFont"));
	}

private:
	int32 EntryIndex = INDEX_NONE;
	TSharedPtr<FShaderAuditSession> Session;
	const TArray<FName>* ColumnIds = nullptr;
	TArray<FString> CachedColumns;
};

// ============================================================================
// SSHKEntryListWidget
// ============================================================================

void SSHKEntryListWidget::Construct(const FArguments& InArgs)
{
	// Parse column IDs from the canonical header line
	{
		const FString Header = FStableShaderKeyAndValue::HeaderLine();
		TArray<FString> Parts;
		Header.ParseIntoArray(Parts, TEXT(","), false);
		for (const FString& Part : Parts)
		{
			ColumnIds.Add(FName(*Part));
		}
	}

	// Build header row
	TSharedPtr<SHeaderRow> HeaderRow = SNew(SHeaderRow);
	for (const FName& ColId : ColumnIds)
	{
		HeaderRow->AddColumn(
			SHeaderRow::Column(ColId)
			.DefaultLabel(FText::FromName(ColId))
			.FillWidth(1.f)
		);
	}

	// Keyboard shortcut: Ctrl+C
	CommandList = MakeShared<FUICommandList>();
	CommandList->MapAction(
		FGenericCommands::Get().Copy,
		FExecuteAction::CreateSP(this, &SSHKEntryListWidget::CopySelectedRows),
		FCanExecuteAction::CreateSP(this, &SSHKEntryListWidget::HasSelection));

	ChildSlot
	[
		SNew(SVerticalBox)

		// Filter bar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.f, 2.f)
		[
			SNew(SHorizontalBox)

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
				.HintText(LOCTEXT("FilterHint", "Filter: asset.class == Material && shader.type contains BasePass"))
				.OnTextCommitted(FOnTextCommitted::CreateSP(this, &SSHKEntryListWidget::OnFilterCommitted))
				.OnShowingSuggestions_Lambda([this](const FString& Text, TArray<FString>& OutSuggestions)
				{
					GetFilterSuggestions(Text, OutSuggestions);
				})
				.ClearKeyboardFocusOnCommit(false)
			]
		]

		// Filter tags
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.f, 0.f)
		[
			SAssignNew(FilterTagBox, SWrapBox)
			.UseAllottedSize(true)
		]

		// Status line
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.f, 2.f)
		[
			SAssignNew(StatusText, STextBlock)
			.Font(FCoreStyle::Get().GetFontStyle("SmallFont"))
		]

		// List view
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SAssignNew(ListView, SListView<TSharedPtr<int32>>)
			.ListItemsSource(&VisibleEntryIndices)
			.OnGenerateRow(this, &SSHKEntryListWidget::OnGenerateRow)
			.SelectionMode(ESelectionMode::Multi)
			.HeaderRow(HeaderRow.ToSharedRef())
			.OnKeyDownHandler_Lambda([this](const FGeometry&, const FKeyEvent& KeyEvent) -> FReply
			{
				return CommandList->ProcessCommandBindings(KeyEvent) ? FReply::Handled() : FReply::Unhandled();
			})
			.OnContextMenuOpening_Lambda([this]() -> TSharedPtr<SWidget>
			{
				if (!HasSelection())
				{
					return nullptr;
				}
				FMenuBuilder MenuBuilder(true, CommandList);
				MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy);
				return MenuBuilder.MakeWidget();
			})
		]
	];
}

void SSHKEntryListWidget::SetSession(TSharedPtr<FShaderAuditSession> InSession)
{
	Session = InSession;
	ActiveFilters.Empty();
	ActiveFilterStrings.Empty();

	// Pre-allocate all entry index objects once
	AllEntryIndices.Empty();
	if (Session.IsValid())
	{
		const int32 Total = Session->StableShaderKeyAndValueArray.Num();
		AllEntryIndices.Reserve(Total);
		for (int32 Idx = 0; Idx < Total; ++Idx)
		{
			AllEntryIndices.Add(MakeShared<int32>(Idx));
		}
	}

	RebuildFilterTags();
	ApplyFilters();
}

// ============================================================================
// Filtering
// ============================================================================

void SSHKEntryListWidget::OnFilterCommitted(const FText& Text, ETextCommit::Type CommitType)
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
		return;
	}

	ActiveFilters.Add(MoveTemp(Root));
	ActiveFilterStrings.Add(Expression);
	RebuildFilterTags();
	ApplyFilters();
}

void SSHKEntryListWidget::RemoveFilter(int32 Index)
{
	if (ActiveFilters.IsValidIndex(Index))
	{
		ActiveFilters.RemoveAt(Index);
		ActiveFilterStrings.RemoveAt(Index);
		RebuildFilterTags();
		ApplyFilters();
	}
}

void SSHKEntryListWidget::ApplyFilters()
{
	VisibleEntryIndices.Empty();

	if (!Session.IsValid())
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("NoSession", "No session loaded"));
		}
		if (ListView.IsValid())
		{
			ListView->RequestListRefresh();
		}
		return;
	}

	const int32 Total = Session->StableShaderKeyAndValueArray.Num();

	if (ActiveFilters.Num() == 0)
	{
		// No filters: show all entries (share pre-allocated objects)
		VisibleEntryIndices = AllEntryIndices;
	}
	else
	{
		TBitArray<> Visible;
		BuildVisibleShaders(*Session, ActiveFilters, /*MaxRefCount=*/ 0, Visible);

		for (TConstSetBitIterator<> It(Visible); It; ++It)
		{
			const int32 Idx = It.GetIndex();
			check(AllEntryIndices.IsValidIndex(Idx));
			VisibleEntryIndices.Add(AllEntryIndices[Idx]);
		}
	}

	if (StatusText.IsValid())
	{
		if (ActiveFilters.Num() == 0)
		{
			StatusText->SetText(FText::Format(
				LOCTEXT("StatusAll", "Showing all {0} entries"),
				FText::AsNumber(Total)));
		}
		else
		{
			StatusText->SetText(FText::Format(
				LOCTEXT("StatusFiltered", "Showing {0} of {1} entries"),
				FText::AsNumber(VisibleEntryIndices.Num()),
				FText::AsNumber(Total)));
		}
	}

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

void SSHKEntryListWidget::RebuildFilterTags()
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
						.Text(LOCTEXT("RemoveFilter", "x"))
						.Font(FCoreStyle::Get().GetFontStyle("SmallFont"))
					]
				]
			]
		];
	}
}

// ============================================================================
// Copy/paste
// ============================================================================

FString SSHKEntryListWidget::FormatEntryAsCSV(int32 EntryIndex) const
{
	check(Session.IsValid());
	check(Session->StableShaderKeyAndValueArray.IsValidIndex(EntryIndex));
	return Session->StableShaderKeyAndValueArray[EntryIndex].ToString();
}

void SSHKEntryListWidget::CopySelectedRows()
{
	if (!Session.IsValid() || !ListView.IsValid())
	{
		return;
	}

	TArray<TSharedPtr<int32>> Selected = ListView->GetSelectedItems();
	if (Selected.Num() == 0)
	{
		return;
	}

	FString Clipboard;

	// Header row
	FString Header = FStableShaderKeyAndValue::HeaderLine();
	Clipboard += Header;
	Clipboard += TEXT("\n");

	// Data rows
	for (const TSharedPtr<int32>& Item : Selected)
	{
		if (Item.IsValid())
		{
			Clipboard += FormatEntryAsCSV(*Item);
			Clipboard += TEXT("\n");
		}
	}

	FPlatformApplicationMisc::ClipboardCopy(*Clipboard);
}

bool SSHKEntryListWidget::HasSelection() const
{
	return ListView.IsValid() && ListView->GetNumItemsSelected() > 0;
}

// ============================================================================
// Row generation
// ============================================================================

TSharedRef<ITableRow> SSHKEntryListWidget::OnGenerateRow(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SSHKEntryRow, OwnerTable)
		.EntryIndex(Item.IsValid() ? *Item : 0)
		.Session(Session)
		.ColumnIds(&ColumnIds);
}

// ============================================================================
// Filter suggestions (lightweight version)
// ============================================================================

void SSHKEntryListWidget::GetFilterSuggestions(const FString& Text, TArray<FString>& OutSuggestions)
{
	GetShaderFilterSuggestions(Text, Session.Get(), OutSuggestions);
}

#undef LOCTEXT_NAMESPACE
