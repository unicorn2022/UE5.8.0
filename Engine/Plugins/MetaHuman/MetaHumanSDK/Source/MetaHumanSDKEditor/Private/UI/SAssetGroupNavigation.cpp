// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAssetGroupNavigation.h"

#include "MetaHumanAssetReport.h"
#include "MetaHumanSDKSettings.h"
#include "ProjectUtilities/MetaHumanAssetManager.h"
#include "UI/MetaHumanStyleSet.h"

#include "Framework/Application/SlateApplication.h"
#include "Misc/PathViews.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"

#define LOCTEXT_NAMESPACE "AssetGroupNavigation"

namespace UE::MetaHuman
{

namespace Private
{
	TMap<EMetaHumanAssetType, FText> SectionNames = {
		{EMetaHumanAssetType::Character, LOCTEXT("CharacterAssetNavigationSection", "Characters (Editable)")},
		{EMetaHumanAssetType::CharacterAssembly, LOCTEXT("CharacterAssemblyNavigationSection", "Characters (Assembly)")},
		{EMetaHumanAssetType::SkeletalClothing, LOCTEXT("SkeletalClothingNavigationSection", "Clothing (Skeletal)")},
		{EMetaHumanAssetType::OutfitClothing, LOCTEXT("OutfitClothingNavigationSection", "Clothing (Outfit)")},
		{EMetaHumanAssetType::Groom, LOCTEXT("GroomsNavigationSection", "Grooms")},
		{EMetaHumanAssetType::None, FText::GetEmpty()}
	};
}

// ─── SNavigationEntry ────────────────────────────────────────────────────────

void SNavigationEntry::Construct(const FArguments& Args, const TSharedRef<STableViewBase>& Owner)
{
	RowData = Args._Item;
	IsMultiSelectModeAttr = Args._IsMultiSelectMode;
	IsCheckedAttr = Args._IsChecked;
	OnCheckChangedCallback = Args._OnCheckChanged;

	STableRow::Construct(
		STableRow::FArguments()
		.Content()
		[
			SNew(SHorizontalBox)

			// Checkbox — only visible in multi-select mode
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(4.f, 0.f, 2.f, 0.f))
			[
				SNew(SCheckBox)
				.Visibility(this, &SNavigationEntry::GetCheckBoxVisibility)
				.IsChecked_Lambda([this]()
				{
					return IsCheckedAttr.Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this](ECheckBoxState)
				{
					OnCheckChangedCallback.ExecuteIfBound();
				})
			]

			// Verification status icon
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(TAttribute<FMargin>::CreateSP(this, &SNavigationEntry::GetMarginForItem))
			.AutoWidth()
			[
				SNew(SImage)
				.Image(this, &SNavigationEntry::GetIconForItem)
			]

			// Asset name
			+ SHorizontalBox::Slot()
			.FillWidth(1)
			[
				SNew(STextBlock)
				.Font(FMetaHumanStyleSet::Get().GetFontStyle("ItemNavigation.ListItemFont"))
				.Text(FText::FromString(RowData->Name.ToString()))
				.ToolTipText(FText::FromString(RowData->Name.ToString()))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]
		],
		Owner
	);
}

const FSlateBrush* SNavigationEntry::GetIconForItem() const
{
	if (RowData.IsValid() && IsValid(RowData->VerificationReport))
	{
		if (RowData->VerificationReport->GetReportResult() == EMetaHumanOperationResult::Failure)
		{
			return FMetaHumanStyleSet::Get().GetBrush("ReportView.ErrorIcon");
		}
		if (RowData->VerificationReport->HasWarnings())
		{
			return FMetaHumanStyleSet::Get().GetBrush("ReportView.WarningIcon");
		}
		return FMetaHumanStyleSet::Get().GetBrush("ReportView.SuccessIcon");
	}
	return nullptr;
}

FMargin SNavigationEntry::GetMarginForItem() const
{
	if (RowData.IsValid() && IsValid(RowData->VerificationReport))
	{
		return FMetaHumanStyleSet::Get().GetMargin("MetaHumanManager.IconMargin");
	}
	return FMetaHumanStyleSet::Get().GetMargin("MetaHumanManager.NoIconMargin");
}

EVisibility SNavigationEntry::GetCheckBoxVisibility() const
{
	// Use Hidden rather than Collapsed so that the checkbox always occupies its space in the
	// layout. This prevents item rows from shifting width or height when multi-select activates.
	return IsMultiSelectModeAttr.Get() ? EVisibility::Visible : EVisibility::Hidden;
}

// ─── FSectionItem ─────────────────────────────────────────────────────────────

FSectionItem::FSectionItem(const EMetaHumanAssetType InType) :
	Type(InType)
{
}

void FSectionItem::SetItems(const TArray<FMetaHumanAssetDescription>& SourceItems)
{
	bool bNeedsUpdate = false;
	TArray<TSharedRef<FMetaHumanAssetDescription>> NewItems;
	for (const FMetaHumanAssetDescription& Source : SourceItems)
	{
		// Need to duplicate the list as the SListItem API requires arrays of TSharedPtr or TSharedRef for data sources
		NewItems.Emplace(MakeShared<FMetaHumanAssetDescription>(Source));
	}

	NewItems.Sort([](const TSharedPtr<FMetaHumanAssetDescription>& A, const TSharedPtr<FMetaHumanAssetDescription>& B)
	{
		return A->Name.Compare(B->Name) < 0;
	});

	check(!NewItems.IsEmpty()) // There should always be items in the new list, otherwise we would just be destroying this section.

	for (int32 Cursor = 0; Cursor < NewItems.Num();)
	{
		FString PathA = Cursor < Items.Num() ? Items[Cursor]->AssetData.GetObjectPathString() : TEXT("");
		FString PathB = NewItems[Cursor]->AssetData.GetObjectPathString();
		if (PathA == PathB)
		{
			Cursor++;
		}
		else
		{
			bNeedsUpdate = true;
			FName NameA = Cursor < Items.Num() ? Items[Cursor]->Name : NAME_None;
			FName NameB = NewItems[Cursor]->Name;
			if (NameA.IsNone() || NameA.Compare(NameB) > 0)
			{
				Items.Insert(NewItems[Cursor], Cursor);
				Cursor++;
			}
			else
			{
				Items.RemoveAt(Cursor);
			}
		}
	}

	// If required, remove any trailing entries
	if (NewItems.Num() < Items.Num())
	{
		bNeedsUpdate = true;
		Items.RemoveAt(NewItems.Num(), Items.Num() - NewItems.Num());
	}

	if (bNeedsUpdate)
	{
		OnUpdateItems.ExecuteIfBound();
	}
}

const TArray<TSharedRef<FMetaHumanAssetDescription>>& FSectionItem::GetItems() const
{
	return Items;
}

const EMetaHumanAssetType FSectionItem::GetType() const
{
	return Type;
}

const FText& FSectionItem::GetName() const
{
	return Private::SectionNames[Type];
}

void FSectionItem::SetOnUpdateItems(const FOnUpdateItems& Callback)
{
	OnUpdateItems = Callback;
}

// ─── SNavigationSection ───────────────────────────────────────────────────────

void SNavigationSection::Construct(const FArguments& InArgs)
{
	SectionItem = InArgs._SectionItem;
	ExpansionCallback = InArgs._OnExpand;
	NavigateCallback = InArgs._OnNavigate;
	IsMultiSelectModeAttr = InArgs._IsMultiSelectMode;
	CheckedItems = InArgs._CheckedItems;

	RebuildDisplayedItems();

	ChildSlot
	[
		SAssignNew(ExpandableArea, SExpandableArea)
		.HeaderContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(SectionItem->GetName())
				.Font(FMetaHumanStyleSet::Get().GetFontStyle("ItemNavigation.HeaderFont"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
			[
				SNew(STextBlock)
				.Text(this, &SNavigationSection::GetSelectionBadgeText)
				.Visibility(this, &SNavigationSection::GetSelectionBadgeVisibility)
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.8f, 1.0f)))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(2.f, 0.f, 2.f, 0.f))
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.Padding(FMargin(2.f))
				.Visibility(this, &SNavigationSection::GetFolderFilterButtonVisibility)
				.IsEnabled(this, &SNavigationSection::IsFolderFilterPathConfigured)
				.IsChecked(this, &SNavigationSection::GetFolderFilterButtonCheckedState)
				.OnCheckStateChanged(this, &SNavigationSection::OnFolderFilterButtonCheckChanged)
				.ToolTipText(this, &SNavigationSection::GetFolderFilterButtonTooltip)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.Filter"))
					.ColorAndOpacity(FSlateColor::UseForeground())
					.DesiredSizeOverride(FVector2D(12.f, 12.f))
				]
			]
		]
		.HeaderPadding(FMetaHumanStyleSet::Get().GetFloat("ItemNavigation.HeaderPadding"))
		.OnAreaExpansionChanged(FOnBooleanValueChanged::CreateSP(this, &SNavigationSection::OnExpansionChanged))
		.InitiallyCollapsed(InArgs._InitiallyCollapsed)
		.Padding(0)
		.BodyContent()
		[
			// Wrap the list so wide asset names don't push the column's desired width
			// past what the section headers want; the column sizes to the headers.
			SNew(SBox)
			.MaxDesiredWidth(0.0f)
			[
				SAssignNew(ItemsList, SListView<TSharedRef<FMetaHumanAssetDescription>>)
				.ListItemsSource(&DisplayedItems)
				.OnGenerateRow(this, &SNavigationSection::OnGenerateWidgetForItem)
				// Multi-select is handled manually via Ctrl+click; we use Single here so that
				// a plain click on the SListView does not steal focus from our custom logic.
				.SelectionMode(ESelectionMode::Single)
				.OnSelectionChanged(SListView<TSharedRef<FMetaHumanAssetDescription>>::FOnSelectionChanged::CreateSP(
					this, &SNavigationSection::OnSelectionChanged))
			]
		]
	];

	SectionItem->SetOnUpdateItems(FOnUpdateItems::CreateSP(this, &SNavigationSection::OnSectionItemsChanged));

	if (!InArgs._InitiallyCollapsed && !DisplayedItems.IsEmpty())
	{
		ItemsList->SetSelection(DisplayedItems[0]);

		// SetSelection uses ESelectInfo::Direct which is (correctly) filtered out by
		// OnSelectionChanged to avoid interfering with multi-select logic. But for the
		// very first display we need to tell the parent that items exist, so fire the
		// navigate callback explicitly for the initial item.
		FNavigatePayload InitialPayload;
		InitialPayload.DetailItems = {DisplayedItems[0]};
		InitialPayload.MultiSelectItems = {DisplayedItems[0]};
		NavigateCallback.ExecuteIfBound(InitialPayload);
	}
}

TSharedRef<ITableRow> SNavigationSection::OnGenerateWidgetForItem(
	TSharedRef<FMetaHumanAssetDescription> Item,
	const TSharedRef<STableViewBase>& Owner)
{
	const FName ItemKey = Item->AssetData.PackageName;

	return SNew(SNavigationEntry, Owner)
		.Item(Item)
		.IsMultiSelectMode(IsMultiSelectModeAttr)
		.IsChecked_Lambda([this, ItemKey]()
		{
			return CheckedItems != nullptr && CheckedItems->Contains(ItemKey);
		})
		.OnCheckChanged(FSimpleDelegate::CreateSP(this, &SNavigationSection::ToggleChecked, Item));
}

void SNavigationSection::ToggleChecked(TSharedRef<FMetaHumanAssetDescription> Item)
{
	// Toggling the checkbox is equivalent to a Ctrl+click. Signal the parent navigation
	// via the explicit bIsCtrlClick flag so it updates the CheckedItems set accordingly.
	FNavigatePayload Payload;
	Payload.DetailItems = {Item};
	Payload.MultiSelectItems = {Item};
	Payload.bIsCtrlClick = true;
	NavigateCallback.ExecuteIfBound(Payload);
}

void SNavigationSection::OnSelectionChanged(TSharedPtr<FMetaHumanAssetDescription> Item,
                                             ESelectInfo::Type SelectType) const
{
	if (!ExpandableArea->IsExpanded() || !Item.IsValid())
	{
		return;
	}

	// Selections triggered programmatically (e.g. the auto-select of the first item when a
	// section expands, or ClearSelection calls) must not be treated as user clicks — doing so
	// would exit multi-select mode. Only OnMouseClick and OnKeyPress are genuine user actions.
	if (SelectType == ESelectInfo::Direct || SelectType == ESelectInfo::OnNavigation)
	{
		return;
	}

	// Determine whether Ctrl is held — if so this is a multi-select action.
	const bool bCtrlHeld = FSlateApplication::Get().GetModifierKeys().IsControlDown();

	// We signal the parent navigation by passing the clicked item in the first array.
	// The second array (multi-select set) is computed by the parent.
	// We re-use the two-param delegate with a convention:
	//   DetailItem[0] = the clicked item
	//   MultiSelectItems is empty here — the parent resolves it from CheckedItems
	// The parent SAssetGroupNavigation::OnItemClicked handles the rest.
	FNavigatePayload Payload;
	Payload.DetailItems = {Item.ToSharedRef()};
	Payload.bIsCtrlClick = bCtrlHeld;
	// MultiSelectItems is intentionally empty here — the parent SAssetGroupNavigation resolves
	// the full checked set from its authoritative CheckedItems map in OnItemClicked.
	NavigateCallback.ExecuteIfBound(Payload);
}

void SNavigationSection::OnExpansionChanged(bool bIsExpanded) const
{
	ExpansionCallback.ExecuteIfBound(SectionItem, bIsExpanded);
	if (bIsExpanded && !DisplayedItems.IsEmpty())
	{
		ItemsList->SetSelection(DisplayedItems[0]);
	}
}

void SNavigationSection::Collapse()
{
	// Collapse is called by SAssetGroupNavigation to automatically collapse other sections. We don't want to notify
	// SAssetGroupNavigation again so switch out the callback temporarily
	const FOnExpansionChanged OldExpansionCallback = ExpansionCallback;
	ExpansionCallback = {};
	ExpandableArea->SetExpanded(false);
	ItemsList->ClearSelection();
	ExpansionCallback = OldExpansionCallback;
}

void SNavigationSection::RefreshList()
{
	if (ItemsList.IsValid())
	{
		ItemsList->RebuildList();
	}
}

void SNavigationSection::Expand()
{
	if (ExpandableArea.IsValid())
	{
		// SetExpanded fires OnAreaExpansionChanged, which routes through OnExpansionChanged
		// and notifies SAssetGroupNavigation to collapse the other sections.
		ExpandableArea->SetExpanded(true);
	}
}

bool SNavigationSection::SelectAsset(const FAssetData& Asset)
{
	if (!SectionItem.IsValid() || !ItemsList.IsValid())
	{
		return false;
	}

	const FName TargetPackage = Asset.PackageName;
	for (const TSharedRef<FMetaHumanAssetDescription>& Item : DisplayedItems)
	{
		if (Item->AssetData.PackageName == TargetPackage)
		{
			ItemsList->SetSelection(Item);

			// SetSelection above uses ESelectInfo::Direct which OnSelectionChanged
			// intentionally ignores, so fire the navigate callback explicitly to update
			// the parent's detail pane. This mirrors the initial-selection path in Construct.
			FNavigatePayload Payload;
			Payload.DetailItems = {Item};
			Payload.MultiSelectItems = {Item};
			NavigateCallback.ExecuteIfBound(Payload);
			return true;
		}
	}

	return false;
}

int32 SNavigationSection::GetCheckedCountInSection() const
{
	if (CheckedItems == nullptr || CheckedItems->IsEmpty() || !SectionItem.IsValid())
	{
		return 0;
	}

	int32 Count = 0;
	for (const TSharedRef<FMetaHumanAssetDescription>& Item : SectionItem->GetItems())
	{
		if (CheckedItems->Contains(Item->AssetData.PackageName))
		{
			Count++;
		}
	}
	return Count;
}

FText SNavigationSection::GetSelectionBadgeText() const
{
	const int32 Count = GetCheckedCountInSection();
	if (Count > 0)
	{
		return FText::Format(LOCTEXT("SelectionBadge", "({0} selected)"), Count);
	}
	return FText::GetEmpty();
}

EVisibility SNavigationSection::GetSelectionBadgeVisibility() const
{
	// Show the badge when in multi-select mode and this section has checked items.
	// Visible regardless of expanded/collapsed state so the user always sees counts.
	if (IsMultiSelectModeAttr.Get() && GetCheckedCountInSection() > 0)
	{
		return EVisibility::Visible;
	}
	return EVisibility::Collapsed;
}

bool SNavigationSection::IsFolderFilterApplicable() const
{
	return SectionItem.IsValid() && SectionItem->GetType() == EMetaHumanAssetType::SkeletalClothing;
}

bool SNavigationSection::IsFolderFilterPathConfigured() const
{
	const UMetaHumanSDKSettings* Settings = GetDefault<UMetaHumanSDKSettings>();
	return Settings && !Settings->SkeletalClothingPackagingPath.Path.IsEmpty();
}

EVisibility SNavigationSection::GetFolderFilterButtonVisibility() const
{
	return IsFolderFilterApplicable() ? EVisibility::Visible : EVisibility::Collapsed;
}

ECheckBoxState SNavigationSection::GetFolderFilterButtonCheckedState() const
{
	return bFolderFilterActive ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SNavigationSection::OnFolderFilterButtonCheckChanged(ECheckBoxState NewState)
{
	const bool bWasActive = bFolderFilterActive;
	bFolderFilterActive = (NewState == ECheckBoxState::Checked);
	if (bWasActive == bFolderFilterActive)
	{
		return;
	}

	RebuildDisplayedItems();
	if (ItemsList.IsValid())
	{
		ItemsList->RebuildList();
	}
}

FText SNavigationSection::GetFolderFilterButtonTooltip() const
{
	const UMetaHumanSDKSettings* Settings = GetDefault<UMetaHumanSDKSettings>();
	const FString CurrentFolder = Settings ? Settings->SkeletalClothingPackagingPath.Path : FString();
	if (CurrentFolder.IsEmpty())
	{
		return LOCTEXT("FolderFilterTooltipUnconfigured",
			"No Skeletal Clothing filter path set.\n"
			"Configure this folder in Project Settings → MetaHuman SDK → Skeletal Clothing.");
	}
	return FText::Format(
		LOCTEXT("FolderFilterTooltipFmt",
			"Show only Skeletal Clothing assets located under \"{0}\".\n"
			"Configure this folder in Project Settings → MetaHuman SDK → Skeletal Clothing."),
		FText::FromString(CurrentFolder));
}

void SNavigationSection::RebuildDisplayedItems()
{
	DisplayedItems.Reset();
	if (!SectionItem.IsValid())
	{
		return;
	}

	const TArray<TSharedRef<FMetaHumanAssetDescription>>& Source = SectionItem->GetItems();

	if (!bFolderFilterActive || !IsFolderFilterApplicable())
	{
		DisplayedItems = Source;
		return;
	}

	const UMetaHumanSDKSettings* Settings = GetDefault<UMetaHumanSDKSettings>();
	if (!Settings)
	{
		DisplayedItems = Source;
		return;
	}

	const FString& FilterRoot = Settings->SkeletalClothingPackagingPath.Path;
	for (const TSharedRef<FMetaHumanAssetDescription>& Item : Source)
	{
		const FString PackagePath = Item->AssetData.PackagePath.ToString();
		if (FPathViews::IsParentPathOf(FilterRoot, PackagePath))
		{
			DisplayedItems.Add(Item);
		}
	}
}

void SNavigationSection::OnSectionItemsChanged()
{
	RebuildDisplayedItems();
	if (ItemsList.IsValid())
	{
		ItemsList->RebuildList();
	}
}

// ─── SAssetGroupNavigation ────────────────────────────────────────────────────

void SAssetGroupNavigation::Construct(const FArguments& InArgs)
{
	NavigateCallback = InArgs._OnNavigate;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ExpandableArea.Border"))
		.BorderBackgroundColor(FLinearColor::White)
		.Padding(FMetaHumanStyleSet::Get().GetFloat("ItemNavigation.BorderPadding"))
		[
			SAssignNew(SectionsSplitter, SSplitter)
			.PhysicalSplitterHandleSize(2)
			.Orientation(Orient_Vertical)
		]
	];
}

void SAssetGroupNavigation::Refresh()
{
	int32 InsertionCursor = 0;
	InsertionCursor = UpdateSection(InsertionCursor, EMetaHumanAssetType::Character);
	InsertionCursor = UpdateSection(InsertionCursor, EMetaHumanAssetType::CharacterAssembly);
	InsertionCursor = UpdateSection(InsertionCursor, EMetaHumanAssetType::SkeletalClothing);
	InsertionCursor = UpdateSection(InsertionCursor, EMetaHumanAssetType::OutfitClothing);
	UpdateSection(InsertionCursor, EMetaHumanAssetType::Groom);

	// Rebuild AllItems so we can resolve CheckedItems to descriptions.
	AllItems.Reset();
	for (const TSharedRef<FSectionItem>& Section : Sections)
	{
		AllItems.Append(Section->GetItems());
	}

	// Prune CheckedItems that no longer exist (e.g. asset removed from disk between sessions).
	if (bMultiSelectMode)
	{
		TSet<FName> ValidKeys;
		for (const TSharedRef<FMetaHumanAssetDescription>& Item : AllItems)
		{
			ValidKeys.Add(Item->AssetData.PackageName);
		}

		const int32 CountBefore = CheckedItems.Num();
		CheckedItems = CheckedItems.Intersect(ValidKeys);

		if (CheckedItems.IsEmpty())
		{
			bMultiSelectMode = false;
			LastSingleClickItem = nullptr;
		}

		if (CheckedItems.Num() != CountBefore)
		{
			RefreshAllSectionLists();
		}
	}
}

int32 SAssetGroupNavigation::UpdateSection(int32 InsertionCursor, EMetaHumanAssetType Type)
{
	bool bHasSection = Sections.Num() > InsertionCursor && Sections[InsertionCursor]->GetType() == Type;
	const TArray<FMetaHumanAssetDescription> Assets = UMetaHumanAssetManager::FindAssetsForPackaging(Type);
	if (Assets.IsEmpty())
	{
		if (bHasSection)
		{
			// If this section was expanded, lose any selection that might have been there
			if (SectionsSplitter->SlotAt(InsertionCursor).GetSizingRule() == SSplitter::FractionOfParent)
			{
				NavigateCallback.ExecuteIfBound(FNavigatePayload{});
			}
			Sections.RemoveAt(InsertionCursor);
			SectionWidgets.RemoveAt(InsertionCursor);
			SectionsSplitter->RemoveAt(InsertionCursor);
		}
		// Nothing to add, next item will attempt to be inserted at the same location
		return InsertionCursor;
	}

	if (!bHasSection)
	{
		Sections.Insert(MakeShared<FSectionItem>(Type), InsertionCursor);
		Sections[InsertionCursor]->SetItems(Assets);

		TSharedRef<SNavigationSection> NewSection = SNew(SNavigationSection)
			.OnNavigate_Lambda([this](const FNavigatePayload& InPayload)
			{
				if (InPayload.DetailItems.IsEmpty())
				{
					return;
				}
				OnItemClicked(InPayload.DetailItems[0], InPayload.bIsCtrlClick);
			})
			.OnExpand(FOnExpansionChanged::CreateSP(this, &SAssetGroupNavigation::OnExpansionChanged))
			.SectionItem(Sections[InsertionCursor])
			.InitiallyCollapsed(Sections.Num() > 1)
			.IsMultiSelectMode_Lambda([this]() { return bMultiSelectMode; })
			.CheckedItems(&CheckedItems);

		SectionWidgets.Insert(NewSection, InsertionCursor);

		SectionsSplitter->AddSlot(InsertionCursor)
			.SizeRule(SSplitter::SizeToContent)
			.Resizable(false)
			[
				NewSection
			];
	}
	else
	{
		Sections[InsertionCursor]->SetItems(Assets);
	}

	// This location now has the entry for this Type, insert the next section at the next location
	return InsertionCursor + 1;
}

void SAssetGroupNavigation::OnItemClicked(TSharedRef<FMetaHumanAssetDescription> Item, bool bIsCtrlHeld)
{
	if (bIsCtrlHeld)
	{
		// Enter multi-select mode if not already active.
		if (!bMultiSelectMode)
		{
			bMultiSelectMode = true;
			// Preserve the previously single-selected item so it doesn't silently drop out
			// of the selection when the user Ctrl+clicks to enter multi-select mode.
			if (LastSingleClickItem.IsValid())
			{
				CheckedItems.Add(LastSingleClickItem->AssetData.PackageName);
			}
		}

		// Toggle the clicked item.
		const FName Key = Item->AssetData.PackageName;
		if (CheckedItems.Contains(Key))
		{
			CheckedItems.Remove(Key);
			// If nothing is checked any more, exit multi-select.
			if (CheckedItems.IsEmpty())
			{
				bMultiSelectMode = false;
				LastSingleClickItem = nullptr;
				RefreshAllSectionLists();
				// Clear the right-pane selection too.
				NavigateCallback.ExecuteIfBound(FNavigatePayload{});
				return;
			}
		}
		else
		{
			CheckedItems.Add(Key);
		}

		RefreshAllSectionLists();
		FireNavigateCallback(Item);
	}
	else
	{
		// Plain click — exit multi-select mode entirely and record this as the last single selection.
		bMultiSelectMode = false;
		CheckedItems.Reset();
		LastSingleClickItem = Item;

		RefreshAllSectionLists();

		// Single-item navigation: MultiSelectItems mirrors the detail item.
		FNavigatePayload SinglePayload;
		SinglePayload.DetailItems = {Item};
		SinglePayload.MultiSelectItems = {Item};
		NavigateCallback.ExecuteIfBound(SinglePayload);
	}
}

void SAssetGroupNavigation::FireNavigateCallback(TSharedRef<FMetaHumanAssetDescription> DetailItem)
{
	// Build the full multi-select list from CheckedItems.
	TArray<TSharedRef<FMetaHumanAssetDescription>> MultiItems;
	for (const TSharedRef<FMetaHumanAssetDescription>& Candidate : AllItems)
	{
		if (CheckedItems.Contains(Candidate->AssetData.PackageName))
		{
			MultiItems.Add(Candidate);
		}
	}
	FNavigatePayload Payload;
	Payload.DetailItems = {DetailItem};
	Payload.MultiSelectItems = MultiItems;
	NavigateCallback.ExecuteIfBound(Payload);
}

void SAssetGroupNavigation::RefreshAllSectionLists()
{
	for (const TSharedRef<SNavigationSection>& Section : SectionWidgets)
	{
		Section->RefreshList();
	}
}

void SAssetGroupNavigation::SelectAsset(const FAssetData& Asset)
{
	if (!Asset.IsValid())
	{
		return;
	}

	for (const TSharedRef<SNavigationSection>& Section : SectionWidgets)
	{
		// Expand the matching section first so the SListView selection has somewhere to land.
		// Expand cascades through OnExpansionChanged to collapse the other sections.
		Section->Expand();
		if (Section->SelectAsset(Asset))
		{
			return;
		}

		// SelectAsset failed on this section — collapse it again so the next iteration's
		// Expand() doesn't leave us with multiple expanded sections if no match is ever found.
		Section->Collapse();
	}
}

void SAssetGroupNavigation::OnExpansionChanged(TSharedPtr<FSectionItem> ExpandedSection, bool bIsExpanded)
{
	for (int i = 0; i < SectionsSplitter->GetChildren()->Num(); i++)
	{
		if (Sections[i] != ExpandedSection || !bIsExpanded)
		{
			TSharedRef<SWidget> Item = SectionsSplitter->GetChildren()->GetChildAt(i);
			TSharedRef<SNavigationSection> NavigationSection = StaticCastSharedRef<SNavigationSection>(Item);
			NavigationSection->Collapse();
			SectionsSplitter->SlotAt(i).SetSizingRule(SSplitter::SizeToContent);
		}
		else
		{
			SectionsSplitter->SlotAt(i).SetSizingRule(SSplitter::FractionOfParent);
		}
	}
}

} // namespace UE::MetaHuman

#undef LOCTEXT_NAMESPACE
