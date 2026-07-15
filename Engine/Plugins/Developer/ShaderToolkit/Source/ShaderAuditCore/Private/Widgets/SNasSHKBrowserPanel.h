// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NasSHKScanner.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Views/SListView.h"

/**
 * Delegate fired after the panel loads sessions (per-row Load or Browse).
 * The caller should refresh its session UI (e.g. spawn new session tabs).
 */
DECLARE_DELEGATE(FOnSessionsLoaded);


/**
 * A row item in the browser list. Can be either a group header (CL level)
 * or a format entry (child of a group).
 */
struct FNasBrowserItem
{
	enum class EType : uint8 { GroupHeader, FormatEntry };

	EType Type = EType::GroupHeader;

	/** The build group data. */
	TSharedPtr<FNasBuildGroup> Group;

	/** For FormatEntry: the unique format identity (TargetType|FormatName). */
	FString FormatKey;

	/** Get the inventory for this format entry. Returns nullptr for headers or if not found. */
	const FSessionFileInventory* GetInventory() const
	{
		if (Type == EType::FormatEntry && Group.IsValid())
		{
			return Group->Formats.Find(FormatKey);
		}
		return nullptr;
	}

	/** Is this format cached (local paths)? Derived from format key suffix. */
	bool IsCached() const
	{
		return Type == EType::FormatEntry && FNasBuildGroup::IsFormatCached(FormatKey);
	}

	static TSharedPtr<FNasBrowserItem> MakeGroupHeader(TSharedPtr<FNasBuildGroup> InGroup)
	{
		TSharedPtr<FNasBrowserItem> Item = MakeShared<FNasBrowserItem>();
		Item->Type = EType::GroupHeader;
		Item->Group = InGroup;
		return Item;
	}

	static TSharedPtr<FNasBrowserItem> MakeFormatEntry(TSharedPtr<FNasBuildGroup> InGroup, const FString& InFormatKey)
	{
		TSharedPtr<FNasBrowserItem> Item = MakeShared<FNasBrowserItem>();
		Item->Type = EType::FormatEntry;
		Item->Group = InGroup;
		Item->FormatKey = InFormatKey;
		return Item;
	}
};

/** Bit flags controlling which sources StartScan() queries. */
enum class EScanFlags : uint8
{
	None  = 0,
	/** Scan the local cache directory. */
	Local = 1 << 0,
	/** Scan the remote NAS directory. */
	NAS   = 1 << 1,
	/** Scan everything. */
	All   = Local | NAS
};
ENUM_CLASS_FLAGS(EScanFlags);
static constexpr int32 NumScanSources = FMath::CountBits(static_cast<uint32>(EScanFlags::All));

/**
 * Browser panel for discovering SHK files on NAS and local cache.
 *
 * Layout:
 *   Local: /path/to/cache
 *   NAS:   \\server\share\path          [Refresh]
 *   -----------------------------------------------
 *   Branch: [dropdown]           [Clear Cache] [Browse...]
 *   -----------------------------------------------
 *   | CL       | Formats              | Status     |
 *   | 53150985 | v  3 formats         | Cached [X] |  <- group header, click to expand
 *   |          |   PCD3D_SM6          | [x] Cached |  <- format entry with checkbox
 *   |          |   PCD3D_ES31         | [x] NAS    |
 *   | 53150111 | >  2 formats         | NAS        |  <- collapsed
 *   -----------------------------------------------
 *   [Scanning NAS... =========>   ]   (progress bar)
 *   -----------------------------------------------
 *   [Cancel]                            [Load]
 */
class SNasSHKBrowserPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNasSHKBrowserPanel) {}
		SLATE_EVENT(FOnSessionsLoaded, OnSessionsLoaded)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SNasSHKBrowserPanel() override;

	/** True if bytecode discovery is in progress for this group+format. */
	bool IsFormatPending(const FString& GroupKey, const FString& FormatKey) const
	{
		const int32* State = FormatStates.Find(MakePendingKey(GroupKey, FormatKey));
		return State && *State < 0;
	}


private:
	// --- Scanning ---
	void StartScan(EScanFlags Flags = EScanFlags::All);
	void CancelScan();
	/** Clear all scan state (groups, branches, expansion, pending). Call before StartScan for a full refresh. */
	void ResetScanState();
	/**
	 * Refresh local cache state. Removes cached formats from the specified scope,
	 * then kicks a local-only scan via StartScan(EScanFlags::Local).
	 * On completion, re-fires bytecode discovery for expanded groups in scope.
	 */
	void RefreshLocalCache(const FString& GroupKey = FString());
	void OnLocalScanComplete(FNasScanResult Result);
	void OnNasScanProgress(FNasScanResult PartialResult);
	void OnNasScanComplete(FNasScanResult Result);
	/**
	 * Merge a scan result into the live Groups array.
	 * Scanner data is always generation 0 and can never beat enriched data (gen >= 1 from Gather).
	 */
	void MergeScanResult(const FNasScanResult& InResult);

	/**
	 * Try to store an inventory if its generation is high enough.
	 * Higher generation always wins. Returns true if stored.
	 * Both MergeScanResult (scanner data) and ToggleGroupExpanded (Gather data)
	 * use this single entry point so merge logic is centralized.
	 */
	bool TryMergeInventory(const FString& GroupKey, const FString& FormatKey,
		FSessionFileInventory&& Inventory, int32 Generation);
	void RebuildFilteredItems();
	void UpdateBranchList();
	void UpdateStatusText();

	// --- Expand/collapse ---
	void ToggleGroupExpanded(const FString& GroupKey);

	// --- UI callbacks ---
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FNasBrowserItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnBranchSelected(TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo);
	void OnLoadEntry(const FString& GroupKey, const FString& FormatKey);
	void OnClearAllCache();
	void OnClearCachedEntry(const FString& Branch, int32 CL, const FString& TargetType);
	void OnBrowseLocal();
	/** Show confirmation dialog, then execute the clear operation and rescan. */
	void ConfirmAndClearCache(const FText& Message, TFunction<void()> OnConfirmed, const FString& GroupKeyToRefresh = FString());

	// --- Data ---
	FString NasDir;
	FString CacheDir;
	FOnSessionsLoaded OnSessionsLoaded;

	/** Single authoritative map of build groups, keyed by FNasBuildGroup::MakeGroupKey(). */
	TMap<FString, TSharedPtr<FNasBuildGroup>> Groups;
	TArray<FString> Branches;
	bool bNasScanComplete = false;

	/** Cancellation token for async NAS scan. */
	TSharedPtr<std::atomic<bool>> ScanCancelToken;

	/** Branch filter. */
	FString CurrentBranchName;
	TArray<TSharedPtr<FString>> BranchOptions;
	TSharedPtr<FString> SelectedBranch;

	/** Expanded items for the list view. */
	TArray<TSharedPtr<FNasBrowserItem>> DisplayItems;

	/** Which groups are expanded (keyed by "Branch|CL"). */
	TSet<FString> ExpandedGroups;

	/**
	 * Per-inventory generation counter with pending flag.
	 * Keyed by MakePendingKey(GroupKey, FormatKey).
	 *   absent/0 = untouched, generation 0 (scanner data)
	 *   < 0     = Gather in flight (pending); abs(value) = generation
	 *   > 0     = Gather complete (enriched); value = generation
	 * Generation is monotonic: higher abs(value) always wins in TryMergeInventory.
	 * Scanner data uses the snapshot generation (typically 0), so it can never
	 * beat enriched data (generation >= 1).
	 */
	TMap<FString, int32> FormatStates;

	/** Build a unique pending key from group key and format key. */
	static FString MakePendingKey(const FString& GroupKey, const FString& FormatKey)
	{
		return GroupKey + TEXT("::") + FormatKey;
	}

	/** Build root path for bytecode queries. */
	FString BuildRoot;

	/** Per-source generation counters. Bumped by StartScan() for the sources being
	 *  scanned, and by ResetScanState() for all sources. Captured in callback lambdas
	 *  to reject stale callbacks that were already queued on GT before cancellation. */
	TStaticArray<uint32, NumScanSources> ScanGeneration{};

	/** Map a single EScanFlags bit to an array index (0 = Local, 1 = NAS).
	 *  Flag must be exactly one bit — not None, not a combination. */
	static int32 ScanFlagIndex(EScanFlags Flag)
	{
		const uint32 Raw = static_cast<uint32>(Flag);
		checkf(Raw != 0 && FMath::IsPowerOfTwo(Raw), TEXT("ScanFlagIndex requires exactly one bit, got 0x%X"), Raw);
		const int32 Idx = FMath::FloorLog2(Raw);
		check(Idx < NumScanSources);
		return Idx;
	}

	// --- Widgets ---
	TSharedPtr<SListView<TSharedPtr<FNasBrowserItem>>> ListView;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> BranchCombo;
	TSharedPtr<class STextBlock> NasStatusText;
	TSharedPtr<class SProgressBar> LocalProgressBar;
	TSharedPtr<class SProgressBar> NasProgressBar;
};
