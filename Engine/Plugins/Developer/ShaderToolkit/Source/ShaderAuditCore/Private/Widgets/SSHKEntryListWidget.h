// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

struct FShaderAuditSession;
struct FShaderFilterNode;

/**
 * Raw SHK entry list -- an spreadsheet view of all StableShaderKeyAndValue entries.
 *
 * Uses SListView for virtualized rendering (only visible rows get widgets).
 * Filtering is powered by FShaderFilterNode / BuildVisibleShaders.
 * Ctrl+C copies selected rows as comma-separated text.
 */
class SSHKEntryListWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSHKEntryListWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Set (or change) the active session. Rebuilds the entry list. */
	void SetSession(TSharedPtr<FShaderAuditSession> InSession);

private:
	/** Rebuild VisibleEntryIndices from ActiveFilters, then refresh the list. */
	void ApplyFilters();

	/** Rebuild the filter tag pills in the tag bar. */
	void RebuildFilterTags();

	/** Called when the user presses Enter in the filter box. */
	void OnFilterCommitted(const FText& Text, ETextCommit::Type CommitType);

	/** Remove a filter by index. */
	void RemoveFilter(int32 Index);

	/** Copy selected rows to clipboard as comma-separated text. */
	void CopySelectedRows();

	/** True if at least one row is selected. */
	bool HasSelection() const;

	/** Format a single SHK entry as a comma-separated row string. */
	FString FormatEntryAsCSV(int32 EntryIndex) const;

	/** Row generator callback for SListView. */
	TSharedRef<class ITableRow> OnGenerateRow(TSharedPtr<int32> Item, const TSharedRef<class STableViewBase>& OwnerTable);

	/** Populate autocomplete suggestions for the filter text box. */
	void GetFilterSuggestions(const FString& Text, TArray<FString>& OutSuggestions);

	// --- Data ---
	TSharedPtr<FShaderAuditSession> Session;

	/** Column IDs parsed from FStableShaderKeyAndValue::HeaderLine(). */
	TArray<FName> ColumnIds;

	/** All entry indices (pre-allocated once on SetSession). */
	TArray<TSharedPtr<int32>> AllEntryIndices;

	/** Indices into Session->StableShaderKeyAndValueArray for visible (filtered) entries. */
	TArray<TSharedPtr<int32>> VisibleEntryIndices;

	/** The list view widget. */
	TSharedPtr<SListView<TSharedPtr<int32>>> ListView;

	/** Status text: "Showing X of Y entries". */
	TSharedPtr<class STextBlock> StatusText;

	// --- Filters ---
	TArray<FShaderFilterNode> ActiveFilters;
	TArray<FString> ActiveFilterStrings;
	TSharedPtr<class SWrapBox> FilterTagBox;
	TSharedPtr<class SSearchBox> HashSearchBox;

	/** Command list for keyboard shortcuts (Ctrl+C). */
	TSharedPtr<class FUICommandList> CommandList;
};
