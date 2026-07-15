// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCharacterPaletteItem.h"
#include "MetaHumanCharacterPipeline.h"

#include "Framework/SlateDelegates.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STileView.h"

class FAssetThumbnail;
class FAssetThumbnailPool;
class SSearchBox;
class SWrapBox;
class SOverlay;
class UMetaHumanInstance;
class UMetaHumanCollection;

struct FCollectionItemTileData;

/**
 * A Content Browser-like view of all items across all slots in a MetaHuman Collection.
 */
class SCollectionItemTileView : public SCompoundWidget
{
public:
	typedef typename TSlateDelegates<TSharedPtr<FMetaHumanCharacterPaletteItem>>::FOnSelectionChanged FOnSelectionChanged;
	typedef TDelegate<void(TSharedPtr<FMetaHumanCharacterPaletteItem>, FName)> FOnMouseButtonDoubleClick;
	typedef FSimpleDelegate FOnCollectionModified;

	SLATE_BEGIN_ARGS(SCollectionItemTileView) 
	{}
		SLATE_ARGUMENT(UMetaHumanInstance*, MetaHumanInstance)
		/** 
		 * This widget supports editing a Collection that is different from the MetaHumanInstance's
		 * Collection.
		 * 
		 * If this argument is null, the MetaHumanInstance's Collection will be used.
		 */
		SLATE_ARGUMENT_DEFAULT(UMetaHumanCollection*, MetaHumanCollection) = nullptr;
		/** True if this asset is allowed to edit the Collection, otherwise it will only view the Collection's contents */
		SLATE_ARGUMENT_DEFAULT(bool, IsCollectionEditable) = false;
		SLATE_EVENT(FOnSelectionChanged, OnSelectionChanged)
		SLATE_EVENT(FOnMouseButtonDoubleClick, OnMouseButtonDoubleClick)
		SLATE_EVENT(FOnCollectionModified, OnCollectionModified)
	SLATE_END_ARGS()

	SCollectionItemTileView();

	void Construct(const FArguments& Args);

	virtual bool SupportsKeyboardFocus() const override { return true; }

	/**
	 * Write the edited item back to the Collection asset.
	 *
	 * @param bAffectsBuild  True if the Collection build should be invalidated by this change.
	 */
	void WriteItemToCollection(
		const FMetaHumanPaletteItemKey& OriginalItemKey,
		const TSharedRef<FMetaHumanCharacterPaletteItem>& ModifiedItem,
		bool bAffectsBuild = true);

	/** Refresh the slot filter buttons and items after the pipeline specification has changed */
	void Refresh();

	/** Refresh the state that determines whether each tile is selectable or not */
	void InvalidateSelectableState();

	void OnPreviewBuildComplete(bool bSucceeded);

	// Begin SWidget interface
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	// End SWidget interface

private:
	/** Populate AllItems from the collection */
	void PopulateAllItems();

	/** Rebuild FilteredItems from AllItems based on current filters */
	void RebuildFilteredItems();

	/** Build the slot filter buttons from the current pipeline specification */
	void BuildSlotFilterButtons();

	/** Slot filter button callbacks */
	void OnSlotFilterChanged(ECheckBoxState NewState, FName SlotName);
	ECheckBoxState IsSlotFilterChecked(FName SlotName) const;

	/** Search box callback */
	void OnSearchTextChanged(const FText& InText);

	/** Tile view callbacks */
	TSharedRef<ITableRow> OnGenerateTile(TSharedPtr<FCollectionItemTileData> InItem, const TSharedRef<STableViewBase>& OwnerTable) const;
	void OnTileViewSelectionChanged(TSharedPtr<FCollectionItemTileData> SelectedTile, ESelectInfo::Type SelectInfo);
	void OnTileViewDoubleClick(TSharedPtr<FCollectionItemTileData> SelectedTile);

	/** Context menu */
	TSharedPtr<SWidget> OnTileViewContextMenuOpening();
	void OnDeleteAction();

	/** State queries */
	bool IsItemActive(TSharedPtr<const FCollectionItemTileData> TileData) const;
	bool IsItemSelectable(TSharedPtr<const FCollectionItemTileData> TileData) const;
	FLinearColor GetSlotColorForItem(TSharedPtr<const FCollectionItemTileData> TileData) const;
	FLinearColor GetTintColorForTile(TSharedPtr<const FCollectionItemTileData> TileData) const;

	/** Drag and drop helpers */
	TArray<FName> GetTargetSlotNames() const;

	/** Compute which visible slots accept the asset types being dragged */
	TArray<FName> GetCompatibleSlotsForDrag(const FDragDropEvent& DragDropEvent) const;

	/** Show/hide the drop zone overlay */
	void ShowDropZoneOverlay(const TArray<FName>& CompatibleSlots);
	void HideDropZoneOverlay();

	/** Empty-state overlay. Shown when there are no items to display in the tile view. */
	EVisibility GetEmptyStateVisibility() const;
	FText GetEmptyStateAcceptedTypesText() const;

	/** Returns the visible slot names currently in scope for the empty-state hint text. */
	TArray<FName> GetEmptyStateScopeSlotNames() const;

	/** Recompute and cache the accepted asset type names for the empty-state hint text, based on current slot filters. */
	void RebuildEmptyStateAcceptedTypeNames();

	TStrongObjectPtr<UMetaHumanInstance> MetaHumanInstance;
	TStrongObjectPtr<UMetaHumanCollection> MetaHumanCollection;
	FOnSelectionChanged OnSelectionChangedDelegate;
	FOnMouseButtonDoubleClick OnMouseButtonDoubleClickDelegate;
	FOnCollectionModified OnCollectionModifiedDelegate;
	bool bIsCollectionEditable = false;

	/** All items across all visible slots */
	TArray<TSharedPtr<FCollectionItemTileData>> AllItems;

	/** Selections deferred until the build completes */
	struct FDeferredSlotSelection
	{
		FName SlotName;
		FMetaHumanPaletteItemKey ItemKey;
	};
	TArray<FDeferredSlotSelection> PendingAutoSelections;

	/** Filtered items currently shown in the tile view */
	TSharedRef<UE::Slate::Containers::TObservableArray<TSharedPtr<FCollectionItemTileData>>> FilteredItems;

	TSharedRef<FAssetThumbnailPool> AssetThumbnailPool;
	TSharedPtr<STileView<TSharedPtr<FCollectionItemTileData>>> TileView;
	TSharedPtr<SWrapBox> SlotFilterBox;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<class FUICommandList> ContextMenuCommandList;

	/** Currently active slot filters. Empty means show all. */
	TSet<FName> ActiveSlotFilters;


	/**
	 * Cached display names of asset classes accepted by the slots currently in scope for the
	 * empty-state hint text.
	 */
	TArray<FString> EmptyStateAcceptedTypeNames;

	/** Current search text */
	FString SearchText;

	/** Cached slot colors from pipeline specification, keyed by slot name */
	TMap<FName, FLinearColor> SlotColors;

	/** Drop zone overlay state */
	TArray<FName> DragCompatibleSlots;
	TSharedPtr<SWidget> DropZoneOverlay;
	TSharedPtr<SOverlay> TileViewOverlay;
};
