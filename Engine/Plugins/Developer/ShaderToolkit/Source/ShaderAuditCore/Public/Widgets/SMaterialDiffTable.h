// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

struct FShaderAuditSession;
struct FMaterialDiffNode;
using FMaterialDiffNodePtr = TSharedPtr<FMaterialDiffNode>;
class STableViewBase;
class STextBlock;
class ITableRow;

/** Caller-supplied hook to navigate to an asset (typically: focus content browser, or open the asset
 *  editor when bCtrlDown is true). Unbound -> click does nothing. */
DECLARE_DELEGATE_TwoParams(FOnNavigateToShaderAsset, const FString& /*AssetPath*/, bool /*bCtrlDown*/);

class SMaterialDiffTable : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMaterialDiffTable) {}
		SLATE_ARGUMENT(TSharedPtr<FShaderAuditSession>, SessionA)
		SLATE_ARGUMENT(TSharedPtr<FShaderAuditSession>, SessionB)

		/** Optional hook invoked when the user activates an asset hyperlink. */
		SLATE_EVENT(FOnNavigateToShaderAsset, OnNavigateToAsset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void DiffSessions(TSharedPtr<FShaderAuditSession> InSessionA, TSharedPtr<FShaderAuditSession> InSessionB);

private:
	TArray<FMaterialDiffNodePtr> Items;
	TArray<FMaterialDiffNodePtr> FilteredItems;
	TSharedPtr<FShaderAuditSession> SessionA;
	TSharedPtr<FShaderAuditSession> SessionB;

	TSharedPtr<SListView<FMaterialDiffNodePtr>> ListView;

	// --- Editor hooks (bound by caller via Slate args) ---
	FOnNavigateToShaderAsset OnNavigateToAssetHook;
	TSharedPtr<STextBlock> SummaryText;

	// Status filter toggles
	bool bShowNew = true;
	bool bShowRemoved = true;
	bool bShowChanged = true;
	bool bShowSame = false;

	// Column sorting
	FName SortColumn;
	EColumnSortMode::Type SortMode = EColumnSortMode::None;
	void OnSortColumnHeader(EColumnSortPriority::Type Priority, const FName& Column, EColumnSortMode::Type NewSortMode);
	EColumnSortMode::Type GetSortModeForColumn(FName Column) const;

	// Dynamic header text for numeric columns
	TSharedPtr<STextBlock> HeaderB1Text;
	TSharedPtr<STextBlock> HeaderB2Text;
	TSharedPtr<STextBlock> HeaderDeltaText;

	TSharedRef<ITableRow> OnGenerateRow(FMaterialDiffNodePtr InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void OnFilterTextChanged(const FText& InFilterText);
	void RefreshFilteredItems();
	void ExportFilteredItemsToCSV();
	void ApplyRegressionPreset();

	FText FilterText;
	int32 MinDeltaThreshold = 0;
};
