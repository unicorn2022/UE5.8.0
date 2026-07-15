// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Internationalization/Text.h"
#include "ProjectUtilities/MetaHumanAssetManager.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

enum class EMetaHumanAssetType : uint8;
class SExpandableArea;
struct FMetaHumanAssetDescription;

namespace UE::MetaHuman
{

// A Navigation entry in the list - represents a selectable MetaHuman Asset Group
class SNavigationEntry : public STableRow<TSharedRef<FMetaHumanAssetDescription>>
{
public:
	SLATE_BEGIN_ARGS(SNavigationEntry)
		{
		}
		SLATE_ARGUMENT(TSharedPtr<FMetaHumanAssetDescription>, Item)
		/** Whether the navigation is currently in multi-select mode (shows checkboxes). */
		SLATE_ATTRIBUTE(bool, IsMultiSelectMode)
		/** Whether this specific item is currently checked in multi-select mode. */
		SLATE_ATTRIBUTE(bool, IsChecked)
		/** Called when the checkbox state changes for this entry. */
		SLATE_EVENT(FSimpleDelegate, OnCheckChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& Owner);

private:
	TSharedPtr<FMetaHumanAssetDescription> RowData;
	const FSlateBrush* GetIconForItem() const;
	FMargin GetMarginForItem() const;

	/** Checkbox visibility — only shown in multi-select mode. */
	EVisibility GetCheckBoxVisibility() const;

	TAttribute<bool> IsMultiSelectModeAttr;
	TAttribute<bool> IsCheckedAttr;
	FSimpleDelegate OnCheckChangedCallback;
};

DECLARE_DELEGATE(FOnUpdateItems);

// Data class for each section in the navigation list. Represents a collapsible section of the navigation pane
class FSectionItem final
{
public:
	FSectionItem() = default;
	FSectionItem(const EMetaHumanAssetType Type);
	void SetItems(const TArray<FMetaHumanAssetDescription>& SourceItems);
	const TArray<TSharedRef<FMetaHumanAssetDescription>>& GetItems() const;
	const EMetaHumanAssetType GetType() const;
	const FText& GetName() const;
	void SetOnUpdateItems(const FOnUpdateItems& Callback);

private:
	EMetaHumanAssetType Type = EMetaHumanAssetType::None;
	TArray<TSharedRef<FMetaHumanAssetDescription>> Items;
	FOnUpdateItems OnUpdateItems;
};

/**
 * Payload for FOnNavigate. Carries both the item to display in the detail pane and the full
 * operative set (checked items) as first-class named fields, avoiding any implicit encoding
 * conventions that would be invisible to the type system.
 */
struct FNavigatePayload
{
	/** The single item whose details should be shown in the right pane (the last item clicked).
	 *  Empty when nothing is selected. */
	TArray<TSharedRef<FMetaHumanAssetDescription>> DetailItems;

	/** The full set of items that Verify/Package should operate on.
	 *  In single-select mode this mirrors DetailItems (0 or 1 entries).
	 *  In multi-select mode it contains all checked items across all sections. */
	TArray<TSharedRef<FMetaHumanAssetDescription>> MultiSelectItems;

	/** True when the navigation event represents a multi-select toggle (Ctrl+click or
	 *  checkbox change). False for plain clicks and programmatic selections. */
	bool bIsCtrlClick = false;
};

/** Fired by SAssetGroupNavigation whenever the user's selection changes. */
DECLARE_DELEGATE_OneParam(FOnNavigate, const FNavigatePayload&);

DECLARE_DELEGATE_TwoParams(FOnExpansionChanged, TSharedPtr<FSectionItem>, bool);

// Collapsible navigation section expanding to show a list of items
class SNavigationSection final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNavigationSection)
		{
		}
		SLATE_ARGUMENT(TSharedPtr<FSectionItem>, SectionItem)
		SLATE_ARGUMENT(bool, InitiallyCollapsed)
		SLATE_EVENT(FOnNavigate, OnNavigate)
		SLATE_EVENT(FOnExpansionChanged, OnExpand)
		/** Shared multi-select state owned by SAssetGroupNavigation. */
		SLATE_ATTRIBUTE(bool, IsMultiSelectMode)
		SLATE_ARGUMENT(TSet<FName>*, CheckedItems)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void Collapse();

	/** Programmatically expand this section. The parent SAssetGroupNavigation collapses other sections automatically. */
	void Expand();

	/** Rebuild the list so checkbox widgets re-query their state. */
	void RefreshList();

	/**
	 * If this section currently displays an item with the supplied asset data, select it on
	 * the SListView and fire the navigate callback. Returns true if a matching item was
	 * found and selected, false otherwise.
	 */
	bool SelectAsset(const FAssetData& Asset);

private:
	TSharedRef<ITableRow> OnGenerateWidgetForItem(TSharedRef<FMetaHumanAssetDescription> Item,
	                                               const TSharedRef<STableViewBase>& Owner);
	void OnSelectionChanged(const TSharedPtr<FMetaHumanAssetDescription> Item, ESelectInfo::Type Type) const;
	void OnExpansionChanged(bool bIsExpanded) const;

	/** Toggle the checked state of an item and notify the parent navigation. */
	void ToggleChecked(TSharedRef<FMetaHumanAssetDescription> Item);

	/** Returns the number of items in this section that are currently checked. */
	int32 GetCheckedCountInSection() const;

	/** Builds the display text for the selection badge (e.g. "(2 selected)"). */
	FText GetSelectionBadgeText() const;

	/** Badge is visible in multi-select mode when this section has checked items. */
	EVisibility GetSelectionBadgeVisibility() const;

	/** True when this section supports the configured-folder filter (currently SkeletalClothing only). */
	bool IsFolderFilterApplicable() const;

	/** True when the configured Skeletal Clothing folder is non-empty — drives the filter button's enabled state. */
	bool IsFolderFilterPathConfigured() const;

	/** Folder filter button is visible only when IsFolderFilterApplicable() is true. */
	EVisibility GetFolderFilterButtonVisibility() const;

	ECheckBoxState GetFolderFilterButtonCheckedState() const;
	void OnFolderFilterButtonCheckChanged(ECheckBoxState NewState);
	FText GetFolderFilterButtonTooltip() const;

	/** Repopulates DisplayedItems from SectionItem according to the current filter state. */
	void RebuildDisplayedItems();

	/** Called when SectionItem's underlying item list changes. */
	void OnSectionItemsChanged();

	TSharedPtr<FSectionItem> SectionItem;
	TSharedPtr<SExpandableArea> ExpandableArea;
	TSharedPtr<SListView<TSharedRef<FMetaHumanAssetDescription>>> ItemsList;
	FOnNavigate NavigateCallback;
	FOnExpansionChanged ExpansionCallback;

	TAttribute<bool> IsMultiSelectModeAttr;
	/** Non-owning pointer — owned by SAssetGroupNavigation. */
	TSet<FName>* CheckedItems = nullptr;

	/**
	 * Items currently presented to the SListView. Equal to SectionItem's items when no
	 * filter is active; a folder-filtered subset otherwise. The SListView binds to this
	 * array directly so filter toggles only require RebuildDisplayedItems + RebuildList.
	 */
	TArray<TSharedRef<FMetaHumanAssetDescription>> DisplayedItems;

	/** Folder-filter toggle state. Transient — does not persist across window instances. */
	bool bFolderFilterActive = false;
};

// Top-level navigation UI presenting a list of collapsible sections each with a tree underneath
class SAssetGroupNavigation final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssetGroupNavigation)
		{
		}
		SLATE_EVENT(FOnNavigate, OnNavigate)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void OnExpansionChanged(TSharedPtr<FSectionItem> Section, bool bIsExpanded);
	void Refresh();

	/**
	 * Locate the section containing the supplied asset, expand it, and select the matching
	 * item. Does nothing if no section currently lists the asset (e.g. it is not a packageable
	 * MetaHuman type, or the navigation has not been refreshed since the asset was created).
	 */
	void SelectAsset(const FAssetData& Asset);

private:
	int32 UpdateSection(int32 DesiredIndex, EMetaHumanAssetType Type);

	/**
	 * Called by a section when a list item is clicked.
	 * Handles both normal clicks (exit multi-select, set single detail item) and
	 * Ctrl+clicks (enter/extend multi-select, toggle the clicked item's checked state).
	 */
	void OnItemClicked(TSharedRef<FMetaHumanAssetDescription> Item, bool bIsCtrlHeld);

	/** Rebuild all section lists so checkbox visibility/state is refreshed. */
	void RefreshAllSectionLists();

	/** Build the multi-select array from CheckedItems and fire NavigateCallback. */
	void FireNavigateCallback(TSharedRef<FMetaHumanAssetDescription> DetailItem);

	TArray<TSharedRef<FSectionItem>> Sections;
	TArray<TSharedRef<SNavigationSection>> SectionWidgets;
	TSharedPtr<SSplitter> SectionsSplitter;
	FOnNavigate NavigateCallback;

	/** Whether multi-select mode is currently active. */
	bool bMultiSelectMode = false;

	/**
	 * Set of asset package paths that are currently checked.
	 * Keyed by FMetaHumanAssetDescription::AssetData.PackageName.
	 * Populated only during multi-select mode.
	 */
	TSet<FName> CheckedItems;

	/** All items known across all sections — used to resolve CheckedItems to descriptions. */
	TArray<TSharedRef<FMetaHumanAssetDescription>> AllItems;

	/**
	 * The item most recently selected by a plain (non-Ctrl) click.
	 * When the user Ctrl+clicks to enter multi-select mode for the first time, this item
	 * is added to CheckedItems alongside the newly clicked item so it is not silently dropped.
	 */
	TSharedPtr<FMetaHumanAssetDescription> LastSingleClickItem;
};

} // namespace UE::MetaHuman
