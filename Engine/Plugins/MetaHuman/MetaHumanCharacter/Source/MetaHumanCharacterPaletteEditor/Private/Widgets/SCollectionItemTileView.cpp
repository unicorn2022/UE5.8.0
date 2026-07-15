// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SCollectionItemTileView.h"

#include "MetaHumanInstance.h"
#include "MetaHumanCharacterPaletteEditorLog.h"
#include "MetaHumanCharacterPipelineSpecification.h"
#include "MetaHumanCollection.h"
#include "MetaHumanCollectionEditorPipeline.h"
#include "MetaHumanCollectionPipeline.h"
#include "MetaHumanPipelineSlotSelection.h"
#include "MetaHumanPipelineSlotSelectionData.h"
#include "MetaHumanWardrobeItem.h"
#include "MetaHumanCharacterPaletteEditorAnalytics.h"

#include "Algo/RemoveIf.h"
#include "Algo/Transform.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetThumbnail.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Logging/StructuredLog.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterPaletteEditor"

static constexpr float ItemTilePadding = 4.0f;
static constexpr float ItemTileThumbnailSize = 128.0f;
static constexpr float ItemTileViewItemWidth = ItemTileThumbnailSize + ItemTilePadding * 2;
static constexpr float ItemTileNameAreaHeight = 38.0f;
static constexpr float ItemTileViewItemHeight = ItemTileThumbnailSize + ItemTileNameAreaHeight + ItemTilePadding * 2;
static constexpr float ItemTileSlotColorBandHeight = 4.0f;

struct FCollectionItemTileData
{
	FCollectionItemTileData(const TSharedPtr<FMetaHumanCharacterPaletteItem>& InItem, FName InSlotName)
		: Item(InItem)
		, SlotName(InSlotName)
	{
	}

	TSharedPtr<FMetaHumanCharacterPaletteItem> Item;
	TSharedPtr<FAssetThumbnail> Thumbnail;
	FName SlotName;
	bool bIsSelectable = true;
};

// --------------------------------------------------------------------------------------------

/**
 * A simple widget that draws a dashed border rectangle around its content.
 * Used for the drop zone overlay in SCollectionItemTileView.
 */
class SDropZoneDashedBorder : public SBorder
{
public:
	SLATE_BEGIN_ARGS(SDropZoneDashedBorder)
		: _BorderColor(FLinearColor::White)
		, _DashLength(8.0f)
		, _GapLength(6.0f)
		, _BorderThickness(2.0f)
		, _Inset(3.0f)
	{}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(FLinearColor, BorderColor)
		SLATE_ARGUMENT(float, DashLength)
		SLATE_ARGUMENT(float, GapLength)
		SLATE_ARGUMENT(float, BorderThickness)
		SLATE_ARGUMENT(float, Inset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		BorderColor = InArgs._BorderColor;
		DashLength = InArgs._DashLength;
		GapLength = InArgs._GapLength;
		BorderThickness = InArgs._BorderThickness;
		Inset = InArgs._Inset;

		SBorder::Construct(SBorder::FArguments()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(InArgs._BorderColor)
			.Padding(FMargin(8.0f))
			[
				InArgs._Content.Widget
			]
		);
	}

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override
	{
		LayerId = SBorder::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

		// Draw dashed border
		const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
		const float Left = Inset;
		const float Right = LocalSize.X - Inset;
		const float Top = Inset;
		const float Bottom = LocalSize.Y - Inset;

		struct FEdge { FVector2D Start; FVector2D End; };
		const FEdge Edges[4] = {
			{ FVector2D(Left, Top),     FVector2D(Right, Top) },
			{ FVector2D(Right, Top),    FVector2D(Right, Bottom) },
			{ FVector2D(Right, Bottom), FVector2D(Left, Bottom) },
			{ FVector2D(Left, Bottom),  FVector2D(Left, Top) },
		};

		// Use the same hue as the background but fully opaque for the border line
		const FLinearColor LineColor = FLinearColor(BorderColor.R, BorderColor.G, BorderColor.B, 1.0f);

		for (const FEdge& Edge : Edges)
		{
			const FVector2D Dir = Edge.End - Edge.Start;
			const float EdgeLength = Dir.Size();
			if (EdgeLength < UE_SMALL_NUMBER)
			{
				continue;
			}

			const FVector2D UnitDir = Dir / EdgeLength;
			float Cursor = 0.0f;

			while (Cursor < EdgeLength)
			{
				const float SegEnd = FMath::Min(Cursor + DashLength, EdgeLength);

				TArray<FVector2D> Points;
				Points.Add(Edge.Start + UnitDir * Cursor);
				Points.Add(Edge.Start + UnitDir * SegEnd);

				FSlateDrawElement::MakeLines(
					OutDrawElements,
					LayerId,
					AllottedGeometry.ToPaintGeometry(),
					Points,
					ESlateDrawEffect::None,
					LineColor,
					true,
					BorderThickness);

				Cursor = SegEnd + GapLength;
			}
		}

		return LayerId;
	}

private:
	FLinearColor BorderColor;
	float DashLength;
	float GapLength;
	float BorderThickness;
	float Inset;
};

SCollectionItemTileView::SCollectionItemTileView()
	: FilteredItems(MakeShared<UE::Slate::Containers::TObservableArray<TSharedPtr<FCollectionItemTileData>>>())
	, AssetThumbnailPool(MakeShared<FAssetThumbnailPool>(256))
{
}

void SCollectionItemTileView::Construct(const FArguments& Args)
{
	check(Args._MetaHumanInstance);
	MetaHumanInstance = TStrongObjectPtr(Args._MetaHumanInstance);
	MetaHumanCollection = TStrongObjectPtr(Args._MetaHumanCollection);
	bIsCollectionEditable = Args._IsCollectionEditable;
	OnSelectionChangedDelegate = Args._OnSelectionChanged;
	OnMouseButtonDoubleClickDelegate = Args._OnMouseButtonDoubleClick;
	OnCollectionModifiedDelegate = Args._OnCollectionModified;

	if (!MetaHumanCollection)
	{
		MetaHumanCollection = TStrongObjectPtr(MetaHumanInstance->GetMetaHumanCollection());
		check(MetaHumanCollection);
	}

	// Set up context menu commands
	ContextMenuCommandList = MakeShareable(new FUICommandList);
	ContextMenuCommandList->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SCollectionItemTileView::OnDeleteAction));

	// Populate items
	PopulateAllItems();

	// Build the UI
	this->ChildSlot
	[
		SNew(SVerticalBox)

		// Slot filter buttons
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 2.0f)
		[
			SAssignNew(SlotFilterBox, SWrapBox)
			.UseAllottedSize(true)
			.InnerSlotPadding(FVector2D(4.0f, 4.0f))
		]

		// Search box
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 2.0f)
		[
			SAssignNew(SearchBox, SSearchBox)
			.OnTextChanged(this, &SCollectionItemTileView::OnSearchTextChanged)
		]

		// Tile view (wrapped in overlay for drop zones)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4.0f, 2.0f)
		[
			SAssignNew(TileViewOverlay, SOverlay)

			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SAssignNew(TileView, STileView<TSharedPtr<FCollectionItemTileData>>)
					.ListItemsSource(FilteredItems)
					.ItemWidth(ItemTileViewItemWidth)
					.ItemHeight(ItemTileViewItemHeight)
					.OnGenerateTile(this, &SCollectionItemTileView::OnGenerateTile)
					.OnSelectionChanged(this, &SCollectionItemTileView::OnTileViewSelectionChanged)
					.OnMouseButtonDoubleClick(this, &SCollectionItemTileView::OnTileViewDoubleClick)
					.OnContextMenuOpening(this, &SCollectionItemTileView::OnTileViewContextMenuOpening)
				]
			]

			// Empty-state hint shown when the filtered tile view has no items.
			// Bound to be visible only when no tiles are showing AND no drop-zone overlay is active.
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SNew(SBox)
				.Visibility(this, &SCollectionItemTileView::GetEmptyStateVisibility)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				.Padding(FMargin(16.0f))
				[
					SNew(SDropZoneDashedBorder)
					.BorderColor(FLinearColor(0.4f, 0.4f, 0.4f, 0.15f))
					[
						SNew(STextBlock)
						.Text(TAttribute<FText>::CreateSP(this, &SCollectionItemTileView::GetEmptyStateAcceptedTypesText))
						.Justification(ETextJustify::Center)
						.AutoWrapText(true)
						.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f, 1.0f)))
					]
				]
			]

		]
	];

	BuildSlotFilterButtons();

	// Apply initial filter (show all)
	RebuildFilteredItems();

	RebuildEmptyStateAcceptedTypeNames();

	InvalidateSelectableState();
}

void SCollectionItemTileView::BuildSlotFilterButtons()
{
	check(SlotFilterBox.IsValid());

	// Clear existing buttons and cached colors
	SlotFilterBox->ClearChildren();
	SlotColors.Empty();

	const UMetaHumanCollectionPipeline* Pipeline = MetaHumanCollection->GetPipeline();
	if (!Pipeline)
	{
		return;
	}

	// Cache slot colors from the pipeline specification
	TNotNull<const UMetaHumanCharacterPipelineSpecification*> Spec = Pipeline->GetSpecification();
	for (const TPair<FName, FMetaHumanCharacterPipelineSlot>& SlotPair : Spec->Slots)
	{
		SlotColors.Add(SlotPair.Key, SlotPair.Value.SlotColor);
	}

	// "All" button first
	SlotFilterBox->AddSlot()
	[
		SNew(SBox)
		.Padding(FMargin(0.0f))
		[
			SNew(SCheckBox)
			.Style(FAppStyle::Get(), "DetailsView.SectionButton")
			.OnCheckStateChanged(this, &SCollectionItemTileView::OnSlotFilterChanged, FName("All"))
			.IsChecked(this, &SCollectionItemTileView::IsSlotFilterChecked, FName("All"))
			[
				SNew(STextBlock)
				.TextStyle(FAppStyle::Get(), "SmallText")
				.Text(LOCTEXT("AllFilterButton", "All"))
			]
		]
	];

	for (const TPair<FName, FMetaHumanCharacterPipelineSlot>& SlotPair : Spec->Slots)
	{
		if (!SlotPair.Value.bVisibleToUser)
		{
			continue;
		}

		SlotFilterBox->AddSlot()
		[
			SNew(SBox)
			.Padding(FMargin(0.0f))
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "DetailsView.SectionButton")
				.OnCheckStateChanged(this, &SCollectionItemTileView::OnSlotFilterChanged, SlotPair.Key)
				.IsChecked(this, &SCollectionItemTileView::IsSlotFilterChecked, SlotPair.Key)
				[
					SNew(STextBlock)
					.TextStyle(FAppStyle::Get(), "SmallText")
					.Text(FText::FromName(SlotPair.Key))
				]
			]
		];
	}
}

void SCollectionItemTileView::Refresh()
{
	ActiveSlotFilters.Reset();
	BuildSlotFilterButtons();
	PopulateAllItems();
	RebuildFilteredItems();
	RebuildEmptyStateAcceptedTypeNames();
	InvalidateSelectableState();
}

void SCollectionItemTileView::InvalidateSelectableState()
{
	for (const TSharedPtr<FCollectionItemTileData>& TileData : AllItems)
	{
		if (TileData.IsValid())
		{
			// Default to selectable, so if we can't determine the selectability the tile will be undimmmed
			TileData->bIsSelectable = true;
		}
	}

	if (!MetaHumanInstance.IsValid())
	{
		return;
	}

	UMetaHumanCollection* InstanceCollection = MetaHumanInstance->GetMetaHumanCollection();
	if (!InstanceCollection)
	{
		return;
	}

	const UMetaHumanCollectionPipeline* Pipeline = InstanceCollection->GetPipeline();
	if (!Pipeline)
	{
		return;
	}

	// AreSlotSelectionsAllowed can only be called if the collection has been built
	if (!InstanceCollection->GetBuiltData().IsValid())
	{
		return;
	}

	const UMetaHumanCharacterPipelineSpecification* Spec = Pipeline->GetSpecification();

	// Compute the propagated real-slot selections once for the whole pass.
	const TArray<FMetaHumanPipelineSlotSelectionData> RealSlotSelectionData =
		InstanceCollection->PropagateVirtualSlotSelections(MetaHumanInstance->GetSlotSelectionData());

	TArray<FMetaHumanPipelineSlotSelection> BaseRealSlotSelections;
	BaseRealSlotSelections.Reserve(RealSlotSelectionData.Num());
	Algo::Transform(RealSlotSelectionData, BaseRealSlotSelections,
		[](const FMetaHumanPipelineSlotSelectionData& Data) { return Data.Selection; });

	for (const TSharedPtr<FCollectionItemTileData>& TileData : AllItems)
	{
		if (!TileData.IsValid() || !TileData->Item.IsValid())
		{
			continue;
		}

		const TOptional<FName> RealSlotNameMaybe = Spec->ResolveRealSlotName(TileData->SlotName);
		if (!RealSlotNameMaybe.IsSet())
		{
			TileData->bIsSelectable = false;
			continue;
		}

		const FName RealSlotName = RealSlotNameMaybe.GetValue();
		const FMetaHumanCharacterPipelineSlot* SlotSpec = Spec->Slots.Find(RealSlotName);
		if (!SlotSpec)
		{
			TileData->bIsSelectable = false;
			continue;
		}

		const FMetaHumanPaletteItemKey ProposedItemKey = TileData->Item->GetItemKey();
		TArray<FMetaHumanPipelineSlotSelection> ProposedSelections = BaseRealSlotSelections;

		// Find out if this item is already selected
		bool bAlreadySelected = false;
		for (const FMetaHumanPipelineSlotSelection& Existing : BaseRealSlotSelections)
		{
			if (Existing.SlotName == RealSlotName
				&& Existing.SelectedItem == ProposedItemKey)
			{
				bAlreadySelected = true;
				break;
			}
		}

		// If the item is already selected, we still call AreSlotSelectionsAllowed to see if it's
		// a valid selection, but skip removing and re-adding it.
		if (!bAlreadySelected)
		{
			// If this is a single-select slot, remove any existing direct selections for it.
			if (!SlotSpec->bAllowsMultipleSelection)
			{
				const int32 NumToKeep = Algo::RemoveIf(ProposedSelections,
					[RealSlotName](const FMetaHumanPipelineSlotSelection& Selection)
					{
						return Selection.SlotName == RealSlotName && Selection.ParentItemPath.IsEmpty();
					});
				ProposedSelections.SetNum(NumToKeep);
			}

			ProposedSelections.Add(FMetaHumanPipelineSlotSelection(RealSlotName, ProposedItemKey));
		}

		FText DisallowedReason;
		TileData->bIsSelectable = Pipeline->AreSlotSelectionsAllowed(
			InstanceCollection,
			ProposedSelections,
			DisallowedReason);
	}
}

void SCollectionItemTileView::OnPreviewBuildComplete(bool bSucceeded)
{
	// If the build succeeded, apply any selections that were queued up

	if (PendingAutoSelections.IsEmpty())
	{
		return;
	}

	if (!bSucceeded || !MetaHumanInstance.IsValid())
	{
		PendingAutoSelections.Reset();
		return;
	}

	UMetaHumanCollection* InstanceCollection = MetaHumanInstance->GetMetaHumanCollection();
	const UMetaHumanCollectionPipeline* Pipeline = InstanceCollection ? InstanceCollection->GetPipeline() : nullptr;
	if (!Pipeline
		|| !InstanceCollection->GetBuiltData().IsValid())
	{
		PendingAutoSelections.Reset();
		return;
	}

	const UMetaHumanCharacterPipelineSpecification* Spec = Pipeline->GetSpecification();

	bool bAnyApplied = false;
	for (const FDeferredSlotSelection& Pending : PendingAutoSelections)
	{
		const TOptional<FName> RealSlotNameMaybe = Spec->ResolveRealSlotName(Pending.SlotName);
		if (!RealSlotNameMaybe.IsSet())
		{
			continue;
		}

		const FName RealSlotName = RealSlotNameMaybe.GetValue();
		const FMetaHumanCharacterPipelineSlot* SlotSpec = Spec->Slots.Find(RealSlotName);
		if (!SlotSpec)
		{
			continue;
		}

		// Build the proposed real-slot selection set: existing selections, plus the
		// newly added item, with any prior single-select entries for the same slot removed.
		const TArray<FMetaHumanPipelineSlotSelectionData> RealSlotSelectionData =
			InstanceCollection->PropagateVirtualSlotSelections(MetaHumanInstance->GetSlotSelectionData());

		TArray<FMetaHumanPipelineSlotSelection> ProposedSelections;
		ProposedSelections.Reserve(RealSlotSelectionData.Num() + 1);
		Algo::Transform(RealSlotSelectionData, ProposedSelections,
			[](const FMetaHumanPipelineSlotSelectionData& Data) { return Data.Selection; });

		if (!SlotSpec->bAllowsMultipleSelection)
		{
			const int32 NumToKeep = Algo::RemoveIf(ProposedSelections,
				[RealSlotName](const FMetaHumanPipelineSlotSelection& Selection)
				{
					return Selection.SlotName == RealSlotName && Selection.ParentItemPath.IsEmpty();
				});
			ProposedSelections.SetNum(NumToKeep);
		}

		ProposedSelections.Add(FMetaHumanPipelineSlotSelection(RealSlotName, Pending.ItemKey));

		FText DisallowedReason;
		if (Pipeline->AreSlotSelectionsAllowed(InstanceCollection, ProposedSelections, DisallowedReason))
		{
			MetaHumanInstance->SetSingleSlotSelection(Pending.SlotName, Pending.ItemKey);
			bAnyApplied = true;
		}
	}

	PendingAutoSelections.Reset();

	if (bAnyApplied)
	{
		InvalidateSelectableState();
	}
}

void SCollectionItemTileView::PopulateAllItems()
{
	AllItems.Reset();

	const UMetaHumanCollectionPipeline* Pipeline = MetaHumanCollection->GetPipeline();
	if (!Pipeline)
	{
		return;
	}

	TNotNull<const UMetaHumanCharacterPipelineSpecification*> Spec = Pipeline->GetSpecification();

	for (const FMetaHumanCharacterPaletteItem& Item : MetaHumanCollection->GetItems())
	{
		// Only include items whose slot is visible to the user
		const FMetaHumanCharacterPipelineSlot* SlotSpec = Spec->Slots.Find(Item.SlotName);
		if (SlotSpec && SlotSpec->bVisibleToUser)
		{
			AllItems.Add(MakeShared<FCollectionItemTileData>(
				MakeShared<FMetaHumanCharacterPaletteItem>(Item),
				Item.SlotName));
		}
	}
}

void SCollectionItemTileView::RebuildFilteredItems()
{
	FilteredItems->Reset(AllItems.Num());

	for (const TSharedPtr<FCollectionItemTileData>& TileData : AllItems)
	{
		if (!TileData.IsValid() || !TileData->Item.IsValid())
		{
			continue;
		}

		// Slot filter
		if (!ActiveSlotFilters.IsEmpty() && !ActiveSlotFilters.Contains(TileData->SlotName))
		{
			continue;
		}

		// Text search filter
		if (!SearchText.IsEmpty())
		{
			const FString DisplayName = TileData->Item->GetOrGenerateDisplayName().ToString();
			if (!DisplayName.Contains(SearchText))
			{
				continue;
			}
		}

		FilteredItems->Add(TileData);
	}

	if (TileView.IsValid())
	{
		TileView->RequestListRefresh();
	}
}

void SCollectionItemTileView::OnSlotFilterChanged(ECheckBoxState NewState, FName SlotName)
{
	const bool bIsControlDown = FSlateApplication::Get().GetModifierKeys().IsControlDown();

	if (SlotName == FName("All"))
	{
		// "All" always clears the filter set
		ActiveSlotFilters.Reset();
	}
	else if (NewState == ECheckBoxState::Unchecked)
	{
		if (bIsControlDown)
		{
			ActiveSlotFilters.Remove(SlotName);
		}
		else
		{
			ActiveSlotFilters.Reset();
			ActiveSlotFilters.Add(SlotName);
		}
	}
	else // Checked
	{
		if (!bIsControlDown)
		{
			ActiveSlotFilters.Reset();
		}
		ActiveSlotFilters.Add(SlotName);
	}

	RebuildFilteredItems();
	RebuildEmptyStateAcceptedTypeNames();
}

ECheckBoxState SCollectionItemTileView::IsSlotFilterChecked(FName SlotName) const
{
	if (SlotName == FName("All"))
	{
		return ActiveSlotFilters.IsEmpty() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	return ActiveSlotFilters.Contains(SlotName) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SCollectionItemTileView::OnSearchTextChanged(const FText& InText)
{
	SearchText = InText.ToString();
	RebuildFilteredItems();
}

TSharedRef<ITableRow> SCollectionItemTileView::OnGenerateTile(
	TSharedPtr<FCollectionItemTileData> InItem,
	const TSharedRef<STableViewBase>& OwnerTable) const
{
	if (!InItem.IsValid() || !InItem->Item.IsValid())
	{
		return SNew(STableRow<TSharedPtr<FCollectionItemTileData>>, OwnerTable);
	}

	// Resolve thumbnail if not yet created
	if (!InItem->Thumbnail.IsValid())
	{
		IAssetRegistry* AssetRegistry = IAssetRegistry::Get();
		check(AssetRegistry);

		if (InItem->Item->WardrobeItem)
		{
			const FSoftObjectPath PrincipalAssetPath = InItem->Item->WardrobeItem->PrincipalAsset.ToSoftObjectPath();

			FAssetData PrincipalAssetData;
			if (AssetRegistry->TryGetAssetByObjectPath(PrincipalAssetPath, PrincipalAssetData) == UE::AssetRegistry::EExists::Exists)
			{
				InItem->Thumbnail = MakeShared<FAssetThumbnail>(PrincipalAssetData, ItemTileThumbnailSize, ItemTileThumbnailSize, AssetThumbnailPool);
			}
		}
	}

	const FLinearColor TileSlotColor = GetSlotColorForItem(InItem);

	TWeakPtr<const FCollectionItemTileData> WeakTileData = InItem;

	TAttribute<FLinearColor> TileTint = TAttribute<FLinearColor>::CreateLambda(
		[this, WeakTileData]()
		{
			TSharedPtr<const FCollectionItemTileData> PinnedData = WeakTileData.Pin();
			return PinnedData.IsValid() ? GetTintColorForTile(PinnedData) : FLinearColor::White;
		});

	TAttribute<EVisibility> ActiveIconVisibility = TAttribute<EVisibility>::CreateLambda(
		[this, WeakTileData]()
		{
			TSharedPtr<const FCollectionItemTileData> PinnedData = WeakTileData.Pin();
			if (PinnedData.IsValid() && IsItemActive(PinnedData))
			{
				return EVisibility::HitTestInvisible;
			}
			return EVisibility::Collapsed;
		});

	TAttribute<FText> DisplayNameAttribute = TAttribute<FText>::CreateLambda(
		[WeakTileData]()
		{
			TSharedPtr<const FCollectionItemTileData> PinnedData = WeakTileData.Pin();
			if (PinnedData.IsValid() && PinnedData->Item.IsValid())
			{
				return PinnedData->Item->GetOrGenerateDisplayName();
			}
			return FText::GetEmpty();
		});

	// Build the thumbnail content
	TSharedRef<SWidget> ThumbnailContent = InItem->Thumbnail.IsValid()
		? InItem->Thumbnail->MakeThumbnailWidget()
		: StaticCastSharedRef<SWidget>(
			SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(DisplayNameAttribute)
			]
		);

	return SNew(STableRow<TSharedPtr<FCollectionItemTileData>>, OwnerTable)
		.Padding(FMargin(4.0f, 6.0f))
		.Content()
		[
			SNew(SBox)
			.WidthOverride(ItemTileViewItemWidth)
			.HeightOverride(ItemTileViewItemHeight)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("NoBorder"))
				.ColorAndOpacity(TileTint)
				.Padding(FMargin(ItemTilePadding))
				[
					SNew(SVerticalBox)

					// Thumbnail area with overlays
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.HeightOverride(ItemTileThumbnailSize)
						[
							SNew(SOverlay)

							// Thumbnail image
							+ SOverlay::Slot()
							[
								ThumbnailContent
							]

							// Slot color band at bottom of thumbnail
							+ SOverlay::Slot()
							.VAlign(VAlign_Bottom)
							[
								SNew(SBorder)
								.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
								.BorderBackgroundColor(TileSlotColor)
								.Padding(0.0f)
								[
									SNew(SBox)
									.HeightOverride(ItemTileSlotColorBandHeight)
								]
							]

							// Active (eye) icon overlay — bottom right
							+ SOverlay::Slot()
							.HAlign(HAlign_Right)
							.VAlign(VAlign_Bottom)
							.Padding(FMargin(0.0f, 0.0f, 4.0f, ItemTileSlotColorBandHeight + 4.0f))
							[
								SNew(SBorder)
								.BorderImage(FAppStyle::GetBrush("Menu.Background"))
								.Padding(FMargin(3.0f))
								.Visibility(ActiveIconVisibility)
								[
									SNew(SImage)
									.Image(FAppStyle::GetBrush("Icons.Visible"))
									.DesiredSizeOverride(FVector2D(16.0f, 16.0f))
								]
							]
						]
					]

					// Name area
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					.VAlign(VAlign_Center)
					.Padding(0.0f, 2.0f, 0.0f, 2.0f)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(DisplayNameAttribute)
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromName(InItem->SlotName))
							.TextStyle(FAppStyle::Get(), "SmallText")
							.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
						]
					]
				]
			]
		];
}

void SCollectionItemTileView::OnTileViewSelectionChanged(
	TSharedPtr<FCollectionItemTileData> SelectedTile,
	ESelectInfo::Type SelectInfo)
{
	TSharedPtr<FMetaHumanCharacterPaletteItem> SelectedItem;

	if (SelectedTile.IsValid())
	{
		SelectedItem = SelectedTile->Item;
	}

	OnSelectionChangedDelegate.ExecuteIfBound(SelectedItem, SelectInfo);
}

void SCollectionItemTileView::OnTileViewDoubleClick(TSharedPtr<FCollectionItemTileData> SelectedTile)
{
	if (!SelectedTile.IsValid() || !SelectedTile->Item.IsValid())
	{
		return;
	}

	// If this item is already the active selection for its slot, double-clicking should deselect it
	if (IsItemActive(SelectedTile))
	{
		OnMouseButtonDoubleClickDelegate.ExecuteIfBound(nullptr, SelectedTile->SlotName);
		return;
	}

	if (!IsItemSelectable(SelectedTile))
	{
		return;
	}

	OnMouseButtonDoubleClickDelegate.ExecuteIfBound(SelectedTile->Item, SelectedTile->SlotName);
}

TSharedPtr<SWidget> SCollectionItemTileView::OnTileViewContextMenuOpening()
{
	if (!bIsCollectionEditable)
	{
		return nullptr;
	}

	const TArray<TSharedPtr<FCollectionItemTileData>> SelectedTiles = TileView->GetSelectedItems();

	bool bAreAnyValidItemsSelected = false;
	for (const TSharedPtr<FCollectionItemTileData>& SelectedTile : SelectedTiles)
	{
		if (SelectedTile.IsValid() && SelectedTile->Item.IsValid())
		{
			bAreAnyValidItemsSelected = true;
			break;
		}
	}

	if (!bAreAnyValidItemsSelected)
	{
		return nullptr;
	}

	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, ContextMenuCommandList);

	FSlateIcon DummyIcon(NAME_None, NAME_None);
	MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete, NAME_None, TAttribute<FText>(), TAttribute<FText>(), DummyIcon);

	return MenuBuilder.MakeWidget();
}

void SCollectionItemTileView::OnDeleteAction()
{
	if (!bIsCollectionEditable
		|| !MetaHumanCollection->GetEditorPipeline())
	{
		return;
	}

	const TArray<TSharedPtr<FCollectionItemTileData>> SelectedTiles = TileView->GetSelectedItems();

	bool bWasCollectionModified = false;
	for (const TSharedPtr<FCollectionItemTileData>& SelectedTile : SelectedTiles)
	{
		if (!SelectedTile.IsValid() || !SelectedTile->Item.IsValid())
		{
			continue;
		}

		const FMetaHumanPaletteItemKey ItemKey = SelectedTile->Item->GetItemKey();

		if (MetaHumanCollection->TryRemoveItem(ItemKey))
		{
			bWasCollectionModified = true;
			AllItems.Remove(SelectedTile);
		}
		else
		{
			UE_LOGFMT(LogMetaHumanCharacterPaletteEditor, Warning,
				"Failed to remove item {ItemKey} from Collection {Collection}. View must be out of date.",
				ItemKey.ToDebugString(), MetaHumanCollection->GetPathName());
		}
	}

	if (bWasCollectionModified)
	{
		RebuildFilteredItems();
		OnCollectionModifiedDelegate.ExecuteIfBound();
	}
}

bool SCollectionItemTileView::IsItemActive(TSharedPtr<const FCollectionItemTileData> TileData) const
{
	if (!TileData.IsValid() || !TileData->Item.IsValid() || !MetaHumanInstance.IsValid())
	{
		return false;
	}

	const FMetaHumanPaletteItemKey ItemKey = TileData->Item->GetItemKey();
	const FName SlotName = TileData->SlotName;

	for (const FMetaHumanPipelineSlotSelectionData& SelectionData : MetaHumanInstance->GetSlotSelectionData())
	{
		if (SelectionData.Selection.SlotName == SlotName
			&& SelectionData.Selection.SelectedItem == ItemKey)
		{
			return true;
		}
	}

	return false;
}

bool SCollectionItemTileView::IsItemSelectable(TSharedPtr<const FCollectionItemTileData> TileData) const
{
	return TileData.IsValid()
		&& TileData->Item.IsValid()
		&& TileData->bIsSelectable;
}

FLinearColor SCollectionItemTileView::GetSlotColorForItem(TSharedPtr<const FCollectionItemTileData> TileData) const
{
	if (!TileData.IsValid())
	{
		return FLinearColor::White;
	}

	const FLinearColor* FoundColor = SlotColors.Find(TileData->SlotName);
	return FoundColor ? *FoundColor : FLinearColor::White;
}

FLinearColor SCollectionItemTileView::GetTintColorForTile(TSharedPtr<const FCollectionItemTileData> TileData) const
{
	return IsItemSelectable(TileData) ? FLinearColor::White : FLinearColor(0.2f, 0.2f, 0.2f, 1.0f);
}

void SCollectionItemTileView::WriteItemToCollection(
	const FMetaHumanPaletteItemKey& OriginalItemKey,
	const TSharedRef<FMetaHumanCharacterPaletteItem>& ModifiedItem,
	bool bAffectsBuild)
{
	if (!bIsCollectionEditable)
	{
		return;
	}

	const bool bUpdated = bAffectsBuild
		? MetaHumanCollection->TryReplaceItem(OriginalItemKey, ModifiedItem.Get())
		: MetaHumanCollection->TryUpdateItemNonBuildProperties(OriginalItemKey, ModifiedItem.Get());

	if (!bUpdated)
	{
		UE_LOGFMT(LogMetaHumanCharacterPaletteEditor, Error,
			"Failed to update item {Key} in Collection {Collection}",
			OriginalItemKey.ToDebugString(), MetaHumanCollection->GetPathName());
		return;
	}

	// Only notify the listener about changes that affect the Collection build
	if (bAffectsBuild)
	{
		OnCollectionModifiedDelegate.ExecuteIfBound();
	}
}

void SCollectionItemTileView::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (!bIsCollectionEditable || !TileViewOverlay.IsValid())
	{
		return;
	}

	// Only respond to asset drag operations
	TSharedPtr<FAssetDragDropOp> AssetDragDrop = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
	if (!AssetDragDrop)
	{
		return;
	}

	const TArray<FName> CompatibleSlots = GetCompatibleSlotsForDrag(DragDropEvent);
	ShowDropZoneOverlay(CompatibleSlots);
}

void SCollectionItemTileView::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	HideDropZoneOverlay();
}

FReply SCollectionItemTileView::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (DropZoneOverlay.IsValid())
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SCollectionItemTileView::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (DragCompatibleSlots.Num() == 0)
	{
		HideDropZoneOverlay();
		return FReply::Unhandled();
	}

	TSharedPtr<FAssetDragDropOp> AssetDragDrop = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
	if (!AssetDragDrop)
	{
		HideDropZoneOverlay();
		return FReply::Unhandled();
	}

	// Determine which drop zone the mouse is in, using the overlay geometry
	const FGeometry OverlayGeometry = TileViewOverlay.IsValid()
		? TileViewOverlay->GetTickSpaceGeometry()
		: MyGeometry;
	const FVector2D LocalPos = OverlayGeometry.AbsoluteToLocal(DragDropEvent.GetScreenSpacePosition());
	const FVector2D LocalSize = OverlayGeometry.GetLocalSize();
	const int32 NumZones = DragCompatibleSlots.Num();
	const int32 ZoneIndex = (LocalSize.X > 0.0)
		? FMath::Clamp(static_cast<int32>(LocalPos.X / LocalSize.X * NumZones), 0, NumZones - 1)
		: 0;

	const FName TargetSlotName = DragCompatibleSlots[ZoneIndex];

	HideDropZoneOverlay();

	bool bWereAnyAssetsModified = false;
	int32 NumItemsAdded = 0;
	int32 NumItemsImported = 0;
	FAssetData SampleAsset;
	UMetaHumanWardrobeItem* SampleWardrobeItem = nullptr;
	for (const FAssetData& Asset : AssetDragDrop->GetAssets())
	{
		FMetaHumanPaletteItemKey NewItemKey;
		UMetaHumanWardrobeItem* LoadedExternalWardrobeItem = nullptr;
		if (Asset.IsInstanceOf<UMetaHumanWardrobeItem>())
		{
			UMetaHumanWardrobeItem* LoadedWardrobeItem = Cast<UMetaHumanWardrobeItem>(Asset.GetAsset());
			if (!LoadedWardrobeItem)
			{
				continue;
			}

			// Check compatibility up front so we can tell whether this drop will go through the
			// "import" path.
			const UMetaHumanCollectionEditorPipeline* EditorPipeline = MetaHumanCollection->GetEditorPipeline();
			const EMetaHumanWardrobeItemCompatibility Compatibility = EditorPipeline
				? EditorPipeline->TestWardrobeItemCompatibilityWithSlot(TargetSlotName, LoadedWardrobeItem)
				: EMetaHumanWardrobeItemCompatibility::None;

			if (!MetaHumanCollection->TryAddItemFromWardrobeItem(TargetSlotName, LoadedWardrobeItem, NewItemKey))
			{
				continue;
			}

			if (Compatibility == EMetaHumanWardrobeItemCompatibility::Import)
			{
				++NumItemsImported;
			}

			LoadedExternalWardrobeItem = LoadedWardrobeItem;
		}
		else if (!MetaHumanCollection->TryAddItemFromPrincipalAsset(TargetSlotName, Asset.ToSoftObjectPath(), NewItemKey))
		{
			continue;
		}

		check(!NewItemKey.IsNull());

		if (NumItemsAdded == 0)
		{
			SampleAsset = Asset;
			SampleWardrobeItem = LoadedExternalWardrobeItem;

			// For external Wardrobe Items, the asset of interest is the principal asset that the
			// item wraps, not the Wardrobe Item itself. Resolve via the asset registry to avoid
			// loading the principal asset just to record analytics.
			if (LoadedExternalWardrobeItem)
			{
				const FSoftObjectPath PrincipalAssetPath = LoadedExternalWardrobeItem->PrincipalAsset.ToSoftObjectPath();
				if (!PrincipalAssetPath.IsNull())
				{
					IAssetRegistry* AssetRegistry = IAssetRegistry::Get();
					if (AssetRegistry)
					{
						FAssetData PrincipalAssetData;
						if (AssetRegistry->TryGetAssetByObjectPath(PrincipalAssetPath, PrincipalAssetData) == UE::AssetRegistry::EExists::Exists)
						{
							SampleAsset = PrincipalAssetData;
						}
					}
				}
			}
		}
		++NumItemsAdded;

		// Queue the newly added item to be selected after the preview auto-build completes.
		//
		// The selection is queued because we can only determine whether it's allowed to be 
		// selected after the build is done.
		if (MetaHumanCollection->bAutoBuildForPreview
			&& !MetaHumanCollection->GetPipeline()->GetSpecification()->Slots[TargetSlotName].bAllowsMultipleSelection)
		{
			PendingAutoSelections.Add({ TargetSlotName, NewItemKey });
		}

		FMetaHumanCharacterPaletteItem NewItem;
		verify(MetaHumanCollection->TryFindItem(NewItemKey, NewItem));

		AllItems.Add(MakeShared<FCollectionItemTileData>(
			MakeShared<FMetaHumanCharacterPaletteItem>(NewItem),
			TargetSlotName));
		bWereAnyAssetsModified = true;
	}

	if (bWereAnyAssetsModified)
	{
		RebuildFilteredItems();
		OnCollectionModifiedDelegate.ExecuteIfBound();

		UE::MetaHuman::Analytics::RecordDropItemsOnCollectionEvent(
			MetaHumanCollection.Get(),
			TargetSlotName,
			NumItemsAdded,
			SampleAsset,
			SampleWardrobeItem);
	}

	if (NumItemsImported > 0)
	{
		// Surface a toast so the user knows that some Wardrobe Items were cloned into the
		// Collection, rather than referenced as external assets like usual.
		//
		// Otherwise, they may be confused about why the external WI they dragged in is displayed 
		// as "Internal Wardrobe Item".
		const FText Message = NumItemsImported == 1
			? LOCTEXT(
				"WardrobeItemImportedNotification",
				"Wardrobe Item configured for a different pipeline was successfully imported")
			: FText::Format(
				LOCTEXT(
					"WardrobeItemsImportedNotification",
					"{0} Wardrobe Items configured for a different pipeline were successfully imported"),
				FText::AsNumber(NumItemsImported));

		FNotificationInfo Info{ Message };
		Info.FadeInDuration = 0.1f;
		Info.FadeOutDuration = 3.0f;
		Info.ExpireDuration = 5.0f;
		Info.bFireAndForget = true;
		Info.bUseThrobber = false;
		Info.bUseSuccessFailIcons = true;
		Info.bUseLargeFont = true;

		TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
		if (NotificationItem)
		{
			NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
			NotificationItem->ExpireAndFadeout();
		}
	}

	return FReply::Handled();
}

TArray<FName> SCollectionItemTileView::GetTargetSlotNames() const
{
	check(MetaHumanCollection->GetPipeline());

	TArray<FName> SlotNames;
	MetaHumanCollection->GetPipeline()->GetSpecification()->Slots.GetKeys(SlotNames);
	return SlotNames;
}

TArray<FName> SCollectionItemTileView::GetCompatibleSlotsForDrag(const FDragDropEvent& DragDropEvent) const
{
	TArray<FName> CompatibleSlots;

	if (!bIsCollectionEditable || !MetaHumanCollection->GetEditorPipeline())
	{
		return CompatibleSlots;
	}

	TSharedPtr<FAssetDragDropOp> AssetDragDrop = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
	if (!AssetDragDrop)
	{
		return CompatibleSlots;
	}

	const UMetaHumanCollectionEditorPipeline* EditorPipeline = MetaHumanCollection->GetEditorPipeline();
	TNotNull<const UMetaHumanCharacterPipelineSpecification*> Spec = MetaHumanCollection->GetPipeline()->GetSpecification();

	for (const TPair<FName, FMetaHumanCharacterPipelineSlot>& SlotPair : Spec->Slots)
	{
		if (!SlotPair.Value.bVisibleToUser)
		{
			continue;
		}

		bool bSlotIsCompatible = false;
		for (const FAssetData& Asset : AssetDragDrop->GetAssets())
		{
			if (Asset.IsInstanceOf<UMetaHumanWardrobeItem>())
			{
				UMetaHumanWardrobeItem* LoadedWardrobeItem = Cast<UMetaHumanWardrobeItem>(Asset.GetAsset());
				if (LoadedWardrobeItem && EditorPipeline->TestWardrobeItemCompatibilityWithSlot(SlotPair.Key, LoadedWardrobeItem) != EMetaHumanWardrobeItemCompatibility::None)
				{
					bSlotIsCompatible = true;
					break;
				}
			}
			else
			{
				const UClass* AssetClass = Asset.GetClass(EResolveClass::Yes);
				if (AssetClass && EditorPipeline->IsPrincipalAssetClassCompatibleWithSlot(SlotPair.Key, AssetClass))
				{
					bSlotIsCompatible = true;
					break;
				}
			}
		}

		if (bSlotIsCompatible)
		{
			CompatibleSlots.Add(SlotPair.Key);
		}
	}

	return CompatibleSlots;
}

TArray<FName> SCollectionItemTileView::GetEmptyStateScopeSlotNames() const
{
	TArray<FName> ScopeSlots;

	if (!MetaHumanCollection.IsValid())
	{
		return ScopeSlots;
	}

	const UMetaHumanCollectionPipeline* Pipeline = MetaHumanCollection->GetPipeline();
	if (!Pipeline)
	{
		return ScopeSlots;
	}

	TNotNull<const UMetaHumanCharacterPipelineSpecification*> Spec = Pipeline->GetSpecification();

	for (const TPair<FName, FMetaHumanCharacterPipelineSlot>& SlotPair : Spec->Slots)
	{
		if (!SlotPair.Value.bVisibleToUser)
		{
			continue;
		}

		// If the user has narrowed the view with slot filters, only consider those slots.
		if (!ActiveSlotFilters.IsEmpty() && !ActiveSlotFilters.Contains(SlotPair.Key))
		{
			continue;
		}

		ScopeSlots.Add(SlotPair.Key);
	}

	return ScopeSlots;
}

EVisibility SCollectionItemTileView::GetEmptyStateVisibility() const
{
	// Hide while a drop-zone overlay is showing -- the drop zone takes over the empty area.
	if (DragCompatibleSlots.Num() > 0)
	{
		return EVisibility::Collapsed;
	}

	if (FilteredItems->Num() > 0)
	{
		return EVisibility::Collapsed;
	}

	// HitTestInvisible so it doesn't swallow drag events targeting the underlying tile view.
	return EVisibility::HitTestInvisible;
}

void SCollectionItemTileView::RebuildEmptyStateAcceptedTypeNames()
{
	EmptyStateAcceptedTypeNames.Reset();

	if (!bIsCollectionEditable || !MetaHumanCollection.IsValid())
	{
		return;
	}

	const UMetaHumanCollectionEditorPipeline* EditorPipeline = MetaHumanCollection->GetEditorPipeline();
	if (!EditorPipeline)
	{
		return;
	}

	const TArray<FName> ScopeSlots = GetEmptyStateScopeSlotNames();
	if (ScopeSlots.IsEmpty())
	{
		return;
	}

	TNotNull<const UMetaHumanCharacterEditorPipelineSpecification*> EditorSpec = EditorPipeline->GetSpecification();

	// Collect the union of accepted asset classes across the in-scope slots.
	TArray<const UClass*> AcceptedClasses;
	for (const FName& SlotName : ScopeSlots)
	{
		const FMetaHumanCharacterPipelineSlotEditorData* SlotEditorEntry = EditorSpec->SlotEditorData.Find(SlotName);
		if (!SlotEditorEntry)
		{
			continue;
		}

		for (const TSoftClassPtr<UObject>& SoftClass : SlotEditorEntry->SupportedPrincipalAssetTypes)
		{
			const UClass* SupportedClass = SoftClass.LoadSynchronous();
			if (SupportedClass)
			{
				AcceptedClasses.AddUnique(SupportedClass);
			}
		}
	}

	// Format display names, sorted alphabetically for stable ordering.
	EmptyStateAcceptedTypeNames.Reserve(AcceptedClasses.Num());
	for (const UClass* Class : AcceptedClasses)
	{
		EmptyStateAcceptedTypeNames.Add(Class->GetDisplayNameText().ToString());
	}
	EmptyStateAcceptedTypeNames.Sort();
}

FText SCollectionItemTileView::GetEmptyStateAcceptedTypesText() const
{
	// If the user has typed a search and nothing matched, that's a different empty state from
	// "this slot has no items". Say so explicitly rather than inviting them to drop assets.
	if (!SearchText.IsEmpty())
	{
		return FText::Format(
			LOCTEXT("EmptyStateNoSearchMatch", "No items match \"{0}\"."),
			FText::FromString(SearchText));
	}

	// View-only collections -- no drag-drop is allowed, so just say there's nothing to show.
	if (!bIsCollectionEditable)
	{
		return LOCTEXT("EmptyStateNoItemsViewOnly", "No items to display.");
	}

	// No pipeline assigned yet -- without one we can't know what slots or asset types are valid.
	// Tell the user how to fix it.
	if (MetaHumanCollection.IsValid() && !MetaHumanCollection->GetPipeline())
	{
		return LOCTEXT(
			"EmptyStateNoPipeline",
			"When a Pipeline is assigned in the Details panel, assets can be added to the Collection by dragging and dropping them here.");
	}

	const TArray<FName> ScopeSlots = GetEmptyStateScopeSlotNames();
	if (ScopeSlots.IsEmpty())
	{
		return LOCTEXT("EmptyStateNoItems", "No items to display.");
	}

	const FString JoinedTypes = EmptyStateAcceptedTypeNames.IsEmpty()
		? FString()
		: FString::Join(EmptyStateAcceptedTypeNames, TEXT(", "));

	// Build the message:
	//  - all slots in scope: generic "any compatible asset"
	//  - single slot in scope: name the slot
	if (ActiveSlotFilters.IsEmpty())
	{
		if (JoinedTypes.IsEmpty())
		{
			return LOCTEXT("EmptyStateDragHintAllNoTypes", "Drag and drop assets here to add them to this Collection.");
		}

		return FText::Format(
			LOCTEXT("EmptyStateDragHintAll", "Drag and drop assets here to add them to this Collection.\n\nAccepted asset types: {0}"),
			FText::FromString(JoinedTypes));
	}

	if (ScopeSlots.Num() == 1)
	{
		const FText SlotText = FText::FromName(ScopeSlots[0]);

		if (JoinedTypes.IsEmpty())
		{
			return FText::Format(
				LOCTEXT("EmptyStateDragHintSlotNoTypes", "Drag and drop assets here to add them to the '{0}' slot."),
				SlotText);
		}

		return FText::Format(
			LOCTEXT("EmptyStateDragHintSlot", "Drag and drop assets here to add them to the '{0}' slot.\n\nAccepted asset types: {1}"),
			SlotText,
			FText::FromString(JoinedTypes));
	}

	// Multiple filtered slots active.
	if (JoinedTypes.IsEmpty())
	{
		return LOCTEXT("EmptyStateDragHintMultiNoTypes", "Drag and drop assets here to add them to one of the selected slots.");
	}

	return FText::Format(
		LOCTEXT("EmptyStateDragHintMulti", "Drag and drop assets here to add them to one of the selected slots.\n\nAccepted asset types: {0}"),
		FText::FromString(JoinedTypes));
}


void SCollectionItemTileView::ShowDropZoneOverlay(const TArray<FName>& CompatibleSlots)
{
	if (!TileViewOverlay.IsValid())
	{
		return;
	}

	// Remove existing overlay if any
	HideDropZoneOverlay();

	DragCompatibleSlots = CompatibleSlots;

	TSharedRef<SWidget> OverlayContent = SNullWidget::NullWidget;

	if (CompatibleSlots.Num() > 0)
	{
		// Build drop zones — one per compatible slot
		const FLinearColor DropZoneColor = FStyleColors::Primary.GetSpecifiedColor();
		const FLinearColor DropZoneBgColor = FLinearColor(DropZoneColor.R, DropZoneColor.G, DropZoneColor.B, 0.6f);

		TSharedRef<SHorizontalBox> ZoneBox = SNew(SHorizontalBox);

		for (int32 Index = 0; Index < CompatibleSlots.Num(); ++Index)
		{
			const FName SlotName = CompatibleSlots[Index];

			ZoneBox->AddSlot()
			.FillWidth(1.0f)
			[
				SNew(SDropZoneDashedBorder)
				.BorderColor(DropZoneBgColor)
				[
					SNew(STextBlock)
					.Text(FText::FromName(SlotName))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
					.Justification(ETextJustify::Center)
					.ColorAndOpacity(FSlateColor(FLinearColor::White))
				]
			];
		}

		OverlayContent = ZoneBox;
	}
	else
	{
		// No compatible slots — show a grey "not allowed" overlay
		const FLinearColor GreyColor(0.3f, 0.3f, 0.3f, 0.6f);

		OverlayContent = SNew(SDropZoneDashedBorder)
			.BorderColor(GreyColor)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoCompatibleSlots", "No compatible slots"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
				.Justification(ETextJustify::Center)
				.ColorAndOpacity(FSlateColor(FLinearColor::White))
			];
	}

	DropZoneOverlay = OverlayContent;

	TileViewOverlay->AddSlot()
	[
		OverlayContent
	];
}

void SCollectionItemTileView::HideDropZoneOverlay()
{
	DragCompatibleSlots.Reset();

	if (TileViewOverlay.IsValid() && DropZoneOverlay.IsValid())
	{
		TileViewOverlay->RemoveSlot(DropZoneOverlay.ToSharedRef());
		DropZoneOverlay.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
