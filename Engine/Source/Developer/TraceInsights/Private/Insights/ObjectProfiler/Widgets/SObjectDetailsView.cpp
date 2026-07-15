// Copyright Epic Games, Inc. All Rights Reserved.

#include "SObjectDetailsView.h"

#include "Brushes/SlateNoResource.h"
#include "Modules/ModuleManager.h"

#include "SlateOptMacros.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateTypes.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#if WITH_EDITOR
#include "AssetThumbnail.h"
#include "GameFramework/Actor.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#endif

// TraceInsightsCore
#include "InsightsCore/Common/Stopwatch.h"
#include "InsightsCore/Table/ViewModels/BaseTreeNode.h"

// TraceInsights
#include "Insights/InsightsStyle.h"
#include "Insights/ObjectProfiler/IAssetInfoProvider.h"
#include "Insights/ObjectProfiler/ObjectProfilerManager.h"
#include "Insights/ObjectProfiler/ViewModels/AssetInfoNode.h"
#include "Insights/ObjectProfiler/ViewModels/ObjectNode.h"
#include "Insights/ObjectProfiler/Widgets/SObjectTableTreeView.h"

#define LOCTEXT_NAMESPACE "UE::Insights::ObjectProfiler::SObjectDetailsView"

namespace UE::Insights::ObjectProfiler
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// SObjectDetailsViewRow
////////////////////////////////////////////////////////////////////////////////////////////////////

class SObjectDetailsViewRow : public STableRow<TSharedPtr<FAssetInfoNode>>
{
	SLATE_BEGIN_ARGS(SObjectDetailsViewRow) {}
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, TSharedPtr<FAssetInfoNode> InItem, const TSharedRef<STableViewBase>& InOwnerTableView, TWeakPtr<IAssetInfoProvider> InWeakAssetInfoProvider)
	{
		static const FSlateNoResource NoBrush;
		static const FTableRowStyle NoBackgroundRowStyle = FTableRowStyle()
			.SetEvenRowBackgroundBrush(NoBrush)
			.SetEvenRowBackgroundHoveredBrush(NoBrush)
			.SetOddRowBackgroundBrush(NoBrush)
			.SetOddRowBackgroundHoveredBrush(NoBrush)
			.SetSelectorFocusedBrush(NoBrush)
			.SetActiveBrush(NoBrush)
			.SetActiveHoveredBrush(NoBrush)
			.SetInactiveBrush(NoBrush)
			.SetInactiveHoveredBrush(NoBrush);

		WeakAssetInfoProvider = InWeakAssetInfoProvider;

		WeakItem = MoveTemp(InItem);
		bActorNameCached = false;
		CachedActorName = FText::GetEmpty();
		STableRow<TSharedPtr<FAssetInfoNode>>::Construct(
			STableRow<TSharedPtr<FAssetInfoNode>>::FArguments()
			.Style(&NoBackgroundRowStyle)
			.Content()
			[
				SNew(SBox)
				.Padding(FMargin(4.0f, 3.0f))
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SObjectDetailsViewRow::GetAssetNameText)
						.ToolTipText(this, &SObjectDetailsViewRow::GetAssetTooltipText)
					]

					//+ SHorizontalBox::Slot()
					//.AutoWidth()
					//.VAlign(VAlign_Center)
					//.Padding(4.0f, 0.0f, 0.0f, 0.0f)
					//[
					//	SNew(SButton)
					//	.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					//	.ToolTipText(LOCTEXT("BrowseToAssetTooltip", "Browse to asset in Content Browser"))
					//	.OnClicked(this, &SObjectDetailsViewRow::OnBrowseToAssetClicked)
					//	.IsEnabled(this, &SObjectDetailsViewRow::IsBrowseToAssetEnabled)
					//	[
					//		SNew(SImage)
					//		.Image(FAppStyle::Get().GetBrush("Icons.BrowseContent"))
					//	]
					//]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.ToolTipText(LOCTEXT("BrowseToActorTooltip", "Browse to Actor"))
						.OnClicked(this, &SObjectDetailsViewRow::OnBrowseToActorClicked)
						.IsEnabled(this, &SObjectDetailsViewRow::IsBrowseToActorEnabled)
						[
							SNew(SImage)
							.Image(FAppStyle::Get().GetBrush("Icons.BrowseContent"))
						]
					]
				]
			],
			InOwnerTableView);
	}

private:
	FText GetAssetNameText() const
	{
		if (!bActorNameCached)
		{
			TSharedPtr<FAssetInfoNode> Item = WeakItem.Pin();
			if (Item.IsValid())
			{
				bActorNameCached = true;
				FString Name;
#if WITH_EDITOR
				if (UObject* Obj = Item->Actor.GetPath().ResolveObject())
				{
					if (AActor* Actor = Cast<AActor>(Obj))
					{
						Name = Actor->GetActorLabel();
					}
				}
#endif // WITH_EDITOR
				if (Name.IsEmpty())
				{
					// Fallback: strip "PersistentLevel." prefix from sub-path.
					FString SubPath = Item->Actor.GetPath().GetSubPathString();
					int32 LastDotIndex;
					if (SubPath.FindLastChar(TEXT('.'), LastDotIndex))
					{
						Name = SubPath.Mid(LastDotIndex + 1);
					}
					else
					{
						Name = MoveTemp(SubPath);
					}
				}
				CachedActorName = FText::FromString(MoveTemp(Name));
			}
		}
		return CachedActorName;
	}

	FText GetAssetTooltipText() const
	{
		TSharedPtr<FAssetInfoNode> Item = WeakItem.Pin();
		if (!Item.IsValid())
		{
			return FText::GetEmpty();
		}

		const FSoftObjectPath& ActorPath = Item->Actor.GetPath();
		const FString SubPath = ActorPath.GetSubPathString();
		const FString Path = SubPath.IsEmpty()
			? ActorPath.GetAssetPathString()
			: ActorPath.GetAssetPathString() + TEXT(".") + SubPath;
		return FText::FromString(Path);
	}

	bool IsBrowseToAssetEnabled() const
	{
		TSharedPtr<FAssetInfoNode> Item = WeakItem.Pin();
		return Item.IsValid() && Item->AssetData.IsValid();
	}

	FReply OnBrowseToAssetClicked()
	{
		auto AssetInfoProvider = WeakAssetInfoProvider.Pin();
		if (AssetInfoProvider)
		{
			TSharedPtr<FAssetInfoNode> Item = WeakItem.Pin();
			if (Item)
			{
				AssetInfoProvider->BrowseToAsset(*Item);
			}
		}
		return FReply::Handled();
	}

	bool IsBrowseToActorEnabled() const
	{
		TSharedPtr<FAssetInfoNode> Item = WeakItem.Pin();
		return Item.IsValid() && Item->Actor.GetPath().IsValid();
	}

	FReply OnBrowseToActorClicked()
	{
		auto AssetInfoProvider = WeakAssetInfoProvider.Pin();
		if (AssetInfoProvider)
		{
			TSharedPtr<FAssetInfoNode> Item = WeakItem.Pin();
			if (Item)
			{
				AssetInfoProvider->BrowseToActor(*Item);
			}
		}
		return FReply::Handled();
	}

private:
	TWeakPtr<FAssetInfoNode> WeakItem;
	TWeakPtr<IAssetInfoProvider> WeakAssetInfoProvider;
	mutable bool bActorNameCached = false;
	mutable FText CachedActorName;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// SObjectDetailsView
////////////////////////////////////////////////////////////////////////////////////////////////////

SObjectDetailsView::SObjectDetailsView()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SObjectDetailsView::~SObjectDetailsView()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectDetailsView::OnClose()
{
#if WITH_EDITOR
	AssetThumbnail.Reset();
	AssetThumbnailPool.Reset();
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SObjectDetailsView::Construct(const FArguments& InArgs)
{
	SAssignNew(ExternalScrollbar, SScrollBar)
	.AlwaysShowScrollbar(true);

	TSharedPtr<SWidget> ThumbnailWidget;
	constexpr uint32 ThumbnailSize = 64;
#if WITH_EDITOR
	AssetThumbnailPool = UThumbnailManager::Get().GetSharedThumbnailPool();
	if (AssetThumbnailPool.IsValid())
	{
		AssetThumbnail = MakeShared<FAssetThumbnail>(FAssetData(), ThumbnailSize, ThumbnailSize, AssetThumbnailPool);

		FAssetThumbnailConfig ThumbnailConfig;
		ThumbnailConfig.GenericThumbnailSize = (int32)ThumbnailSize;
		ThumbnailConfig.bAllowFadeIn = true;
		ThumbnailConfig.bAllowHintText = false;
		ThumbnailConfig.ShowAssetColor = false;
		ThumbnailConfig.ShowAssetBorder = false;
		ThumbnailConfig.ThumbnailLabel = EThumbnailLabel::NoLabel;

		ThumbnailWidget = AssetThumbnail->MakeThumbnailWidget(ThumbnailConfig);
	}
#endif

	if (!ThumbnailWidget.IsValid())
	{
		ThumbnailWidget = SNew(SBorder)
			.BorderBackgroundColor(FSlateColor::UseForeground())
			[
				SNew(SImage)
				.Image(this, &SObjectDetailsView::GetThumbnail)
			];
	}

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(8.0f, 8.0f, 8.0f, 12.0f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
			[
				SNew(SBox)
				.Visibility(this, &SObjectDetailsView::GetThumbnailVisibility)
				.WidthOverride((float) ThumbnailSize)
				.HeightOverride((float) ThumbnailSize)
				.ToolTipText(this, &SObjectDetailsView::GetThumbnailToolTipText)
				[
					ThumbnailWidget.ToSharedRef()
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Font(FAppStyle::Get().GetFontStyle("Font.Large"))
					.ColorAndOpacity(this, &SObjectDetailsView::GetTitleColor)
					.Text(this, &SObjectDetailsView::GetTitleText)
					.ToolTipText(this, &SObjectDetailsView::GetTitleToolTipText)
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f, 0, 0)
				[
					SNew(STextBlock)
					.Text(this, &SObjectDetailsView::GetSubTitleText)
					.ToolTipText(this, &SObjectDetailsView::GetSubTitleToolTipText)
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
			.Orientation(EOrientation::Orient_Horizontal)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		.Padding(FMargin(8.0f, 12.0f, 8.0f, 8.0f))
		[
			SNew(STextBlock)
			.Visibility(this, &SObjectDetailsView::GetActorsVisibility)
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
			.Text(LOCTEXT("RefBy", "Referenced by"))
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SHorizontalBox)
			.Visibility(this, &SObjectDetailsView::GetActorsVisibility)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(FMargin(5.0f, 0.0f, 0.0f, 0.0f))
			.VAlign(VAlign_Fill)
			[
				SAssignNew(ListView, SListView<TSharedPtr<FAssetInfoNode>>)
				.ListViewStyle(&FInsightsStyle::Get().GetWidgetStyle<FTableViewStyle>("ObjectDetailsView.ListView"))
				.ExternalScrollbar(ExternalScrollbar)
				.SelectionMode(ESelectionMode::Single)
				.ListItemsSource(&AssetEntries)
				.OnGenerateRow(this, &SObjectDetailsView::OnGenerateRow)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f)
			[
				ExternalScrollbar.ToSharedRef()
			]
		]
	];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<ITableRow> SObjectDetailsView::OnGenerateRow(TSharedPtr<FAssetInfoNode> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SObjectDetailsViewRow, InItem, OwnerTable, WeakAssetInfoProvider);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SObjectDetailsView::GetSelectedObjectDetailedToolTip() const
{
	TSharedPtr<SObjectTableTreeView> ObjectTableTreeView = WeakObjectTableTreeView.Pin();
	const bool bHasTotalEstimatedMemory = (ObjectTableTreeView && ObjectTableTreeView->HasTotalEstimatedMemory());

	const FText SystemMemoryText = bHasTotalEstimatedMemory ?
		LOCTEXT("TotalEstimatedSystemMemory", "Total Estimated System Memory") :
		LOCTEXT("EstimatedSystemMemory", "Estimated System Memory");

	const FText VideoMemoryText = bHasTotalEstimatedMemory ?
		LOCTEXT("TotalEstimatedVideoMemory", "Total Estimated Video Memory") :
		LOCTEXT("EstimatedVideoMemory", "Estimated Video Memory");

	FNumberFormattingOptions FormattingOptionsMem;
	FormattingOptionsMem.MaximumFractionalDigits = 2;

	if (SelectedObjectNode.IsValid())
	{
		TUtf8StringBuilder<1024> Str;

		const FString DisplayName = SelectedObjectNode->GetDisplayName().ToString();
		Str.Append(*DisplayName);

		Str.Appendf("\nId: %u", SelectedObjectNode->GetObjectId());

		if (TSharedPtr<FObjectNode> ClassNode = SelectedObjectNode->GetClass())
		{
			Str.Append("\nClass: ");
			Str.Append(ClassNode->GetObjectName());
		}

		if (TSharedPtr<FObjectNode> OuterNode = SelectedObjectNode->GetOuter())
		{
			Str.Append("\nOuter: ");
			Str.Append(OuterNode->GetDisplayName().ToString());
		}

		const uint64 SystemMemorySize = bHasTotalEstimatedMemory ?
			SelectedObjectNode->GetTotalSystemMemorySize() :
			SelectedObjectNode->GetSystemMemorySize();
		const uint64 VideoMemorySize = bHasTotalEstimatedMemory ?
			SelectedObjectNode->GetTotalVideoMemorySize() :
			SelectedObjectNode->GetVideoMemorySize();

		if (SystemMemorySize > 0)
		{
			Str.Append("\n");
			Str.Append(SystemMemoryText.ToString());
			Str.Append(": ");
			Str.Append(FText::AsMemory(SystemMemorySize, &FormattingOptionsMem).ToString());
		}
		if (VideoMemorySize > 0)
		{
			Str.Append("\n");
			Str.Append(VideoMemoryText.ToString());
			Str.Append(": ");
			Str.Append(FText::AsMemory(VideoMemorySize, &FormattingOptionsMem).ToString());
		}

		return FText::FromString(Str.ToString());
	}

	if (SelectedObjectNodes.Num() > 0)
	{
		uint64 SystemMemorySize = 0;
		uint64 VideoMemorySize = 0;

		if (bHasTotalEstimatedMemory)
		{
			for (const TSharedPtr<FObjectNode>& Node : SelectedObjectNodes)
			{
				SystemMemorySize += Node->GetTotalSystemMemorySize();
				VideoMemorySize += Node->GetTotalVideoMemorySize();
			}
		}
		else
		{
			for (const TSharedPtr<FObjectNode>& Node : SelectedObjectNodes)
			{
				SystemMemorySize += Node->GetSystemMemorySize();
				VideoMemorySize += Node->GetVideoMemorySize();
			}
		}

		TUtf8StringBuilder<1024> Str;
		Str.Append(SystemMemoryText.ToString());
		Str.Append(": ");
		Str.Append(FText::AsMemory(SystemMemorySize, &FormattingOptionsMem).ToString());
		Str.Append("\n");
		Str.Append(VideoMemoryText.ToString());
		Str.Append(": ");
		Str.Append(FText::AsMemory(VideoMemorySize, &FormattingOptionsMem).ToString());
		return FText::FromString(Str.ToString());
	}

	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FSlateColor SObjectDetailsView::GetTitleColor() const
{
	if (SelectedObjectNode.IsValid())
	{
		return FSlateColor::UseForeground();
	}

	if (SelectedNodes.Num() > 0)
	{
		return FSlateColor(EStyleColor::AccentFolder);
	}

	return FSlateColor(EStyleColor::White25);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SObjectDetailsView::GetTitleText() const
{
	if (SelectedObjectNode.IsValid())
	{
		return SelectedObjectNode->GetDisplayName();
	}

	if (SelectedNodes.Num() > 0)
	{
		return FText::Format(LOCTEXT("MultipleItemsSelected", "{0} {0}|plural(one=Node,other=Nodes) Selected ({1} {1}|plural(one=Object,other=Objects))"),
			FText::AsNumber(SelectedNodes.Num()),
			FText::AsNumber(SelectedObjectNodes.Num()));
	}

	return LOCTEXT("NoObjectSelected", "No Object Selected");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SObjectDetailsView::GetTitleToolTipText() const
{
	if (SelectedObjectNode.IsValid())
	{
		if (SelectedObjectNode->IsIdentityMasked())
		{
			return LOCTEXT("MaskedObjectTooltip", "This object is internal to Fortnite.");
		}
		return FText::FromString(SelectedObjectNode->GetObjectPath());
	}

	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SObjectDetailsView::GetSubTitleText() const
{
	if (SelectedObjectNodes.Num() > 0)
	{
		FNumberFormattingOptions FormattingOptionsMem;
		FormattingOptionsMem.MaximumFractionalDigits = 2;

		TSharedPtr<SObjectTableTreeView> ObjectTableTreeView = WeakObjectTableTreeView.Pin();
		if (ObjectTableTreeView && ObjectTableTreeView->HasTotalEstimatedMemory())
		{
			uint64 TotalEstimatedMemorySize = 0;
			for (const TSharedPtr<FObjectNode>& Node : SelectedObjectNodes)
			{
				TotalEstimatedMemorySize += Node->GetTotalEstimatedMemorySize();
			}
			return FText::Format(LOCTEXT("TotalSizeFmt", "Total Estimated Size: {0}"), FText::AsMemory(TotalEstimatedMemorySize, &FormattingOptionsMem));
		}
		else
		{
			uint64 EstimatedMemorySize = 0;
			for (const TSharedPtr<FObjectNode>& Node : SelectedObjectNodes)
			{
				EstimatedMemorySize += Node->GetEstimatedMemorySize();
			}
			return FText::Format(LOCTEXT("SizeFmt", "Estimated Size: {0}"), FText::AsMemory(EstimatedMemorySize, &FormattingOptionsMem));
		}
	}
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SObjectDetailsView::GetSubTitleToolTipText() const
{
	return GetSelectedObjectDetailedToolTip();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility SObjectDetailsView::GetThumbnailVisibility() const
{
	return SelectedObjectNode.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SObjectDetailsView::GetThumbnailToolTipText() const
{
	return GetSelectedObjectDetailedToolTip();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FSlateBrush* SObjectDetailsView::GetThumbnail() const
{
	if (SelectedObjectNode.IsValid() &&
		SelectedAssetData.IsValid() &&
		SelectedAssetClassName != NAME_None)
	{
		auto AssetInfoProvider = WeakAssetInfoProvider.Pin();
		if (AssetInfoProvider)
		{
			const FSlateBrush* Brush = AssetInfoProvider->GetThumbnail(SelectedAssetData, SelectedAssetClassName);
			if (Brush)
			{
				return Brush;
			}
			return AssetInfoProvider->GetIcon(SelectedAssetData, SelectedAssetClassName);
		}
	}
	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility SObjectDetailsView::GetActorsVisibility() const
{
	return AssetEntries.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectDetailsView::SetSelectedNodes(const TArray<TSharedPtr<FTableTreeNode>>& InSelectedNodes)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SObjectDetailsView::SetSelectedNodes);

	FStopwatch Stopwatch;
	Stopwatch.Start();

	SelectedNodes = InSelectedNodes;
	SelectedObjectNodes.Reset();

	SelectedObjectNode = nullptr;
	SelectedAssetData = FAssetData();
	SelectedAssetClassName = NAME_None;
	AssetEntries.Reset();

	int32 UniqueActorSetCount = 0;
	UniqueActorSets.Reset();

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SetSelectedNodes_GatherObjectNodes);

		for (const TSharedPtr<FTableTreeNode>& SelectedNode : SelectedNodes)
		{
			GatherObjectNodesRec(SelectedNode);
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SetSelectedNodes_MergeActorSets);

		struct FKeyFuncs : public BaseKeyFuncs<const FActorInfo*, const FActorInfo*>
		{
			FORCEINLINE static const FActorInfo* GetSetKey(const FActorInfo* Element)
			{
				return Element;
			}

			FORCEINLINE static uint32 GetKeyHash(const FActorInfo* Key)
			{
				return GetTypeHash(*Key);
			}

			FORCEINLINE static bool Matches(const FActorInfo* A, const FActorInfo* B)
			{
				return *A == *B;
			}
		};
		TSet<const FActorInfo*, FKeyFuncs> UniqueActors;

		for (const FActorSet* ActorSet : UniqueActorSets)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SetSelectedNodes_MergeActorSet);

			for (const FActorInfo& Actor : ActorSet->GetActors())
			{
				bool bIsAlreadyInSet = false;
				UniqueActors.Add(&Actor, &bIsAlreadyInSet);
				if (!bIsAlreadyInSet)
				{
					auto Entry = MakeShared<FAssetInfoNode>();
					Entry->Actor = Actor;
					AssetEntries.Add(Entry);
				}
			}
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SetSelectedNodes_Sort);

		AssetEntries.Sort(
			[](const TSharedPtr<FAssetInfoNode>& A, const TSharedPtr<FAssetInfoNode>& B)
			{
				return A->Actor.GetPath().LexicalLess(B->Actor.GetPath());
			});
	}

	if (SelectedObjectNodes.Num() == 1)
	{
		SelectedObjectNode = SelectedObjectNodes[0];

		TSharedPtr<FObjectNode> ClassNode = SelectedObjectNode->GetClass();
		SelectedAssetClassName = ClassNode ? FName(ClassNode->GetObjectName()) : NAME_None;

	}
	else if (SelectedObjectNodes.Num() > 1)
	{
		// Reset AssetData if multiple objects are selected.
		SelectedAssetData = FAssetData();
	}

#if WITH_EDITOR
	if (AssetThumbnail.IsValid())
	{
		AssetThumbnail->SetAsset(SelectedAssetData);
	}
#endif

	ListView->RequestListRefresh();

	Stopwatch.Stop();
	const double Duration = Stopwatch.GetAccumulatedTime();
	if (Duration > 0.1)
	{
		UE_LOGF(LogObjectProfiler, Log, "[Obj] Selected %d nodes (%d object nodes, %d actor sets --> %d reference assets) in %.4fs.",
			InSelectedNodes.Num(), SelectedObjectNodes.Num(), UniqueActorSets.Num(), AssetEntries.Num(), Duration);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectDetailsView::GatherObjectNodesRec(TSharedPtr<FBaseTreeNode> Node)
{
	if (Node->Is<FObjectNode>())
	{
		TSharedPtr<FObjectNode> ObjectNode = StaticCastSharedPtr<FObjectNode>(Node);
		AddObjectNode(ObjectNode);
	}

	for (FBaseTreeNodePtr ChildPtr : Node->GetChildren())
	{
		GatherObjectNodesRec(ChildPtr);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SObjectDetailsView::AddObjectNode(TSharedPtr<FObjectNode> InObjectNode)
{
	SelectedObjectNodes.Add(InObjectNode);

	auto AssetInfoProvider = WeakAssetInfoProvider.Pin();
	if (!AssetInfoProvider)
	{
		return;
	}

	AssetInfoProvider->GetAssetData(*InObjectNode.Get(), SelectedAssetData);

	const FActorSet* ActorSet = InObjectNode->GetMatchedActors();
	if (ActorSet)
	{
		UniqueActorSets.Add(ActorSet);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::ObjectProfiler

#undef LOCTEXT_NAMESPACE
