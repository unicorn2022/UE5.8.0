// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMaterialDiffTable.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "ShaderAuditSession.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/ITableRow.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"

static const FName ColumnId_Status(TEXT("Status"));
static const FName ColumnId_Parent(TEXT("Parent"));
static const FName ColumnId_Asset(TEXT("Asset"));
static const FName ColumnId_B1(TEXT("Build One"));
static const FName ColumnId_B2(TEXT("Build Two"));
static const FName ColumnId_Delta(TEXT("Delta"));

enum class EAssetDiffStatus : uint8
{
	Same,
	Changed,
	New,
	Removed
};

struct FMaterialDiffNode : public TSharedFromThis<FMaterialDiffNode>
{
	FString ParentMaterialPath;
	FString FullMaterialPath;
	EAssetDiffStatus Status = EAssetDiffStatus::Same;
	int32 ShadersA = 0;
	int32 ShadersB = 0;
	int32 Delta = 0;

	FMaterialDiffNode(const FString& InParentMaterial, const FString& InAssetPath, EAssetDiffStatus InStatus, int32 InShadersA, int32 InShadersB, int32 InDelta)
	:	ParentMaterialPath(InParentMaterial),
		FullMaterialPath(InAssetPath),
		Status(InStatus),
		ShadersA(InShadersA),
		ShadersB(InShadersB),
		Delta(InDelta)
	{
	}
};

// Extract short asset name from a full object path.
// Paths may have a trailing single-quote from FCompactFullName::ToString() (e.g. "MI_Foo'")
static FString GetShortName(const FString& Path)
{
	FString Name = FPackageName::ObjectPathToObjectName(Path);
	Name.RemoveFromEnd(TEXT("'"));
	return Name;
}

class SMaterialDiffRow : public SMultiColumnTableRow<FMaterialDiffNodePtr>
{
public:
	SLATE_BEGIN_ARGS(SMaterialDiffRow) {}
		SLATE_ARGUMENT(FMaterialDiffNodePtr, Item)
		SLATE_EVENT(FOnNavigateToShaderAsset, OnNavigate)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
	{
		Item = InArgs._Item;
		OnNavigate = InArgs._OnNavigate;

		SMultiColumnTableRow<FMaterialDiffNodePtr>::Construct(
			SMultiColumnTableRow<FMaterialDiffNodePtr>::FArguments()
			.Padding(FMargin(6.0f, 2.0f)),
			InOwnerTableView);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == ColumnId_Status)
		{
			FLinearColor SwatchColor;
			switch (Item->Status)
			{
			case EAssetDiffStatus::New:     SwatchColor = FLinearColor(0.9f, 0.3f, 0.2f); break;
			case EAssetDiffStatus::Removed: SwatchColor = FLinearColor(0.3f, 0.8f, 0.3f); break;
			case EAssetDiffStatus::Changed: SwatchColor = FLinearColor(0.9f, 0.75f, 0.2f); break;
			default:                        SwatchColor = FLinearColor(0.5f, 0.5f, 0.5f); break;
			}

			auto StatusToText = [](EAssetDiffStatus InStatus) -> FText
			{
				switch (InStatus)
				{
				case EAssetDiffStatus::Changed: return FText::FromString(TEXT("Changed"));
				case EAssetDiffStatus::New:     return FText::FromString(TEXT("New"));
				case EAssetDiffStatus::Removed: return FText::FromString(TEXT("Removed"));
				case EAssetDiffStatus::Same:
				default:                        return FText::FromString(TEXT("Same"));
				}
			};

			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("WhiteBrush"))
					.DesiredSizeOverride(FVector2D(8.0f, 8.0f))
					.ColorAndOpacity(SwatchColor)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(StatusToText(Item->Status))
				];
		}

		if (ColumnName == ColumnId_Parent)
		{
			const FString ShortParent = GetShortName(Item->ParentMaterialPath);
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.BrowseContent"))
					.DesiredSizeOverride(FVector2D(12.0f, 12.0f))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SHorizontalBox::Slot()
				.AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SHyperlink)
					.Text(FText::FromString(ShortParent))
					.ToolTipText(FText::FromString(Item->ParentMaterialPath))
					.OnNavigate_Lambda([this]()
					{
						NavigateToAsset(Item->ParentMaterialPath);
					})
				];
		}

		if (ColumnName == ColumnId_Asset)
		{
			const FString ShortAsset = GetShortName(Item->FullMaterialPath);
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.BrowseContent"))
					.DesiredSizeOverride(FVector2D(12.0f, 12.0f))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SHorizontalBox::Slot()
				.AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SHyperlink)
					.Text(FText::FromString(ShortAsset))
					.ToolTipText(FText::FromString(Item->FullMaterialPath))
					.OnNavigate_Lambda([this]()
					{
						NavigateToAsset(Item->FullMaterialPath);
					})
				];
		}

		if (ColumnName == ColumnId_B1)
		{
			return SNew(STextBlock).Text(FText::AsNumber(Item->ShadersA));
		}

		if (ColumnName == ColumnId_B2)
		{
			return SNew(STextBlock).Text(FText::AsNumber(Item->ShadersB));
		}

		if (ColumnName == ColumnId_Delta)
		{
			FString DeltaStr = (Item->Delta > 0)
				? FString::Printf(TEXT("+%d"), Item->Delta)
				: FText::AsNumber(Item->Delta).ToString();

			FSlateColor DeltaColor = FSlateColor::UseForeground();
			if (Item->Delta > 0)
			{
				DeltaColor = FSlateColor(FLinearColor(1.0f, 0.3f, 0.3f));
			}
			else if (Item->Delta < 0)
			{
				DeltaColor = FSlateColor(FLinearColor(0.3f, 1.0f, 0.3f));
			}

			return SNew(STextBlock)
				.Text(FText::FromString(DeltaStr))
				.ColorAndOpacity(DeltaColor);
		}

		return SNullWidget::NullWidget;
	}

private:
	FMaterialDiffNodePtr Item;
	FOnNavigateToShaderAsset OnNavigate;

	void NavigateToAsset(const FString& AssetPath) const
	{
		const bool bCtrlDown = FSlateApplication::Get().GetModifierKeys().IsControlDown();
		OnNavigate.ExecuteIfBound(AssetPath, bCtrlDown);
	}
};

// Build a set of material paths and a path->shader count map from a session
static void BuildMaterialIndex(
	const FShaderAuditSession& Session,
	TSet<FString>& OutPaths,
	TMap<FString, int32>& OutShaderCounts)
{
	for (const FShaderAuditSession::FUniqueMaterial& Mat : Session.UniqueMaterials)
	{
		OutPaths.Add(Mat.Path);
		OutShaderCounts.Add(Mat.Path, Mat.ShaderIndices.Num());
	}
}

// Build a map from material path -> parent package path for O(1) lookup.
static void BuildParentIndex(
	const FShaderAuditSession& Session,
	TMap<FString, FString>& OutParentMap)
{
	for (const FShaderAuditSession::FUniqueMaterial& Mat : Session.UniqueMaterials)
	{
		if (Mat.PackageIndex != INDEX_NONE)
		{
			const int32 ParentPkgIdx = Session.MaterialPackages[Mat.PackageIndex].ParentIndex;
			if (ParentPkgIdx != INDEX_NONE)
			{
				OutParentMap.Add(Mat.Path, Session.MaterialPackages[ParentPkgIdx].PackagePath);
			}
		}
	}
}

static void DiffSessionData(
	FShaderAuditSession* InSessionA,
	FShaderAuditSession* InSessionB,
	TArray<FMaterialDiffNodePtr>& OutItems)
{
	OutItems.Reset();

	if (!InSessionA || !InSessionB)
	{
		return;
	}

	TSet<FString> PathsA, PathsB;
	TMap<FString, int32> CountsA, CountsB;
	BuildMaterialIndex(*InSessionA, PathsA, CountsA);
	BuildMaterialIndex(*InSessionB, PathsB, CountsB);

	// Pre-build parent lookup maps (O(N) once instead of O(N) per query)
	TMap<FString, FString> ParentsA, ParentsB;
	BuildParentIndex(*InSessionA, ParentsA);
	BuildParentIndex(*InSessionB, ParentsB);

	// Resolve parent from whichever session has hierarchy data.
	// Prefer B (the "new" build) since it's more likely to have full data.
	auto FindParent = [&ParentsA, &ParentsB](const FString& AssetPath) -> FString
	{
		if (const FString* Found = ParentsB.Find(AssetPath))
		{
			return *Found;
		}
		if (const FString* Found = ParentsA.Find(AssetPath))
		{
			return *Found;
		}
		return TEXT("N/A");
	};

	// Added in B
	for (const FString& Path : PathsB)
	{
		if (!PathsA.Contains(Path))
		{
			const int32 NumShaders = CountsB.FindRef(Path);
			OutItems.Add(MakeShared<FMaterialDiffNode>(FindParent(Path), Path, EAssetDiffStatus::New, 0, NumShaders, NumShaders));
		}
	}

	// Removed from A
	for (const FString& Path : PathsA)
	{
		if (!PathsB.Contains(Path))
		{
			const int32 NumShaders = CountsA.FindRef(Path);
			OutItems.Add(MakeShared<FMaterialDiffNode>(FindParent(Path), Path, EAssetDiffStatus::Removed, NumShaders, 0, -NumShaders));
		}
		else
		{
			// Present in both
			const int32 NumShadersA = CountsA.FindRef(Path);
			const int32 NumShadersB = CountsB.FindRef(Path);
			const EAssetDiffStatus Status = (NumShadersA != NumShadersB) ? EAssetDiffStatus::Changed : EAssetDiffStatus::Same;
			OutItems.Add(MakeShared<FMaterialDiffNode>(FindParent(Path), Path, Status, NumShadersA, NumShadersB, NumShadersB - NumShadersA));
		}
	}
}

void SMaterialDiffTable::Construct(const FArguments& InArgs)
{
	SessionA = InArgs._SessionA;
	SessionB = InArgs._SessionB;
	OnNavigateToAssetHook = InArgs._OnNavigateToAsset;

	DiffSessionData(SessionA.Get(), SessionB.Get(), Items);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight().Padding(2.0f)
		[
			SNew(SSearchBox)
			.HintText(NSLOCTEXT("MaterialDiff", "FilterHint", "Filter by asset path or material name..."))
			.OnTextChanged(this, &SMaterialDiffTable::OnFilterTextChanged)
		]
		+ SVerticalBox::Slot()
		.AutoHeight().Padding(2.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bShowNew ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bShowNew = (NewState == ECheckBoxState::Checked); RefreshFilteredItems(); })
				[SNew(STextBlock).Text(NSLOCTEXT("MaterialDiff", "FilterNew", "New"))]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bShowRemoved ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bShowRemoved = (NewState == ECheckBoxState::Checked); RefreshFilteredItems(); })
				[SNew(STextBlock).Text(NSLOCTEXT("MaterialDiff", "FilterRemoved", "Removed"))]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bShowChanged ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bShowChanged = (NewState == ECheckBoxState::Checked); RefreshFilteredItems(); })
				[SNew(STextBlock).Text(NSLOCTEXT("MaterialDiff", "FilterChanged", "Changed"))]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bShowSame ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bShowSame = (NewState == ECheckBoxState::Checked); RefreshFilteredItems(); })
				[SNew(STextBlock).Text(NSLOCTEXT("MaterialDiff", "FilterSame", "Same"))]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth().Padding(24.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(STextBlock).Text(NSLOCTEXT("MaterialDiff", "MinDeltaLabel", "Min Delta:"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(60.0f)
					[
						SNew(SSpinBox<int32>)
						.MinValue(0)
						.MaxValue(9999)
						.Value_Lambda([this]() { return MinDeltaThreshold; })
						.OnValueChanged_Lambda([this](int32 NewValue) { MinDeltaThreshold = NewValue; RefreshFilteredItems(); })
					]
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth().Padding(12.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(NSLOCTEXT("MaterialDiff", "RegressionPreset", "Regressions"))
				.ToolTipText(NSLOCTEXT("MaterialDiff", "RegressionPresetTooltip", "Show only New and Changed rows, sorted by Delta descending."))
				.OnClicked_Lambda([this]() -> FReply
				{
					ApplyRegressionPreset();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth().Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(NSLOCTEXT("MaterialDiff", "ExportCSV", "Export CSV"))
				.ToolTipText(NSLOCTEXT("MaterialDiff", "ExportCSVTooltip", "Export the currently filtered rows to a CSV file."))
				.OnClicked_Lambda([this]() -> FReply
				{
					ExportFilteredItemsToCSV();
					return FReply::Handled();
				})
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight().Padding(2.0f)
		[
			SAssignNew(SummaryText, STextBlock)
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(ListView, SListView<FMaterialDiffNodePtr>)
			.ListItemsSource(&FilteredItems)
			.SelectionMode(ESelectionMode::Single)
			.OnGenerateRow(this, &SMaterialDiffTable::OnGenerateRow)
			.HeaderRow
			(
				SNew(SHeaderRow)
				+ SHeaderRow::Column(ColumnId_Status)
					.DefaultLabel(FText::FromString(TEXT("Status")))
					.ManualWidth(80.0f)
					.SortMode(TAttribute<EColumnSortMode::Type>::Create(TAttribute<EColumnSortMode::Type>::FGetter::CreateSP(this, &SMaterialDiffTable::GetSortModeForColumn, ColumnId_Status)))
					.OnSort(FOnSortModeChanged::CreateSP(this, &SMaterialDiffTable::OnSortColumnHeader))
				+ SHeaderRow::Column(ColumnId_Parent)
					.DefaultLabel(FText::FromString(TEXT("Parent")))
					.FillWidth(1.0f)
					.SortMode(TAttribute<EColumnSortMode::Type>::Create(TAttribute<EColumnSortMode::Type>::FGetter::CreateSP(this, &SMaterialDiffTable::GetSortModeForColumn, ColumnId_Parent)))
					.OnSort(FOnSortModeChanged::CreateSP(this, &SMaterialDiffTable::OnSortColumnHeader))
				+ SHeaderRow::Column(ColumnId_Asset)
					.DefaultLabel(FText::FromString(TEXT("Asset")))
					.FillWidth(1.0f)
					.SortMode(TAttribute<EColumnSortMode::Type>::Create(TAttribute<EColumnSortMode::Type>::FGetter::CreateSP(this, &SMaterialDiffTable::GetSortModeForColumn, ColumnId_Asset)))
					.OnSort(FOnSortModeChanged::CreateSP(this, &SMaterialDiffTable::OnSortColumnHeader))
				+ SHeaderRow::Column(ColumnId_B1)
					.HeaderContent()
					[
						SAssignNew(HeaderB1Text, STextBlock).Text(FText::FromString(TEXT("Build One")))
					]
					.ManualWidth(110.0f)
					.HAlignHeader(HAlign_Right)
					.HAlignCell(HAlign_Right)
					.SortMode(TAttribute<EColumnSortMode::Type>::Create(TAttribute<EColumnSortMode::Type>::FGetter::CreateSP(this, &SMaterialDiffTable::GetSortModeForColumn, ColumnId_B1)))
					.OnSort(FOnSortModeChanged::CreateSP(this, &SMaterialDiffTable::OnSortColumnHeader))
				+ SHeaderRow::Column(ColumnId_B2)
					.HeaderContent()
					[
						SAssignNew(HeaderB2Text, STextBlock).Text(FText::FromString(TEXT("Build Two")))
					]
					.ManualWidth(110.0f)
					.HAlignHeader(HAlign_Right)
					.HAlignCell(HAlign_Right)
					.SortMode(TAttribute<EColumnSortMode::Type>::Create(TAttribute<EColumnSortMode::Type>::FGetter::CreateSP(this, &SMaterialDiffTable::GetSortModeForColumn, ColumnId_B2)))
					.OnSort(FOnSortModeChanged::CreateSP(this, &SMaterialDiffTable::OnSortColumnHeader))
				+ SHeaderRow::Column(ColumnId_Delta)
					.HeaderContent()
					[
						SAssignNew(HeaderDeltaText, STextBlock).Text(FText::FromString(TEXT("Delta")))
					]
					.ManualWidth(100.0f)
					.HAlignHeader(HAlign_Right)
					.HAlignCell(HAlign_Right)
					.SortMode(TAttribute<EColumnSortMode::Type>::Create(TAttribute<EColumnSortMode::Type>::FGetter::CreateSP(this, &SMaterialDiffTable::GetSortModeForColumn, ColumnId_Delta)))
					.OnSort(FOnSortModeChanged::CreateSP(this, &SMaterialDiffTable::OnSortColumnHeader))
			)
		]
	];

	RefreshFilteredItems();
}

void SMaterialDiffTable::DiffSessions(TSharedPtr<FShaderAuditSession> InSessionA, TSharedPtr<FShaderAuditSession> InSessionB)
{
	SessionA = InSessionA;
	SessionB = InSessionB;
	DiffSessionData(SessionA.Get(), SessionB.Get(), Items);
	RefreshFilteredItems();
}

TSharedRef<ITableRow> SMaterialDiffTable::OnGenerateRow(FMaterialDiffNodePtr InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SMaterialDiffRow, OwnerTable)
		.Item(InItem)
		.OnNavigate(OnNavigateToAssetHook);
}

void SMaterialDiffTable::OnFilterTextChanged(const FText& InFilterText)
{
	FilterText = InFilterText;
	RefreshFilteredItems();
}

void SMaterialDiffTable::RefreshFilteredItems()
{
	FilteredItems.Reset();

	const FString FilterString = FilterText.ToString();

	int32 TotalShadersA = 0;
	int32 TotalShadersB = 0;
	int32 TotalDelta = 0;
	int32 CountNew = 0;
	int32 CountRemoved = 0;
	int32 CountChanged = 0;
	int32 CountSame = 0;

	for (const FMaterialDiffNodePtr& Item : Items)
	{
		// Text filter
		if (!FilterString.IsEmpty() && !Item->FullMaterialPath.Contains(FilterString))
		{
			continue;
		}

		// Status filter
		switch (Item->Status)
		{
		case EAssetDiffStatus::New:     if (!bShowNew) continue;     CountNew++;     break;
		case EAssetDiffStatus::Removed: if (!bShowRemoved) continue; CountRemoved++; break;
		case EAssetDiffStatus::Changed: if (!bShowChanged) continue; CountChanged++; break;
		case EAssetDiffStatus::Same:    if (!bShowSame) continue;    CountSame++;    break;
		}

		// Delta threshold filter
		if (MinDeltaThreshold > 0 && FMath::Abs(Item->Delta) < MinDeltaThreshold)
		{
			continue;
		}

		FilteredItems.Add(Item);

		TotalShadersA += Item->ShadersA;
		TotalShadersB += Item->ShadersB;
		TotalDelta += Item->Delta;
	}

	// Sort if a sort column is active
	if (SortMode != EColumnSortMode::None && !SortColumn.IsNone())
	{
		const bool bAscending = (SortMode == EColumnSortMode::Ascending);
		Algo::Sort(FilteredItems, [this, bAscending](const FMaterialDiffNodePtr& A, const FMaterialDiffNodePtr& B) -> bool
		{
			int32 Cmp = 0;
			if (SortColumn == ColumnId_Status)
			{
				Cmp = static_cast<int32>(A->Status) - static_cast<int32>(B->Status);
			}
			else if (SortColumn == ColumnId_Parent)
			{
				Cmp = A->ParentMaterialPath.Compare(B->ParentMaterialPath, ESearchCase::IgnoreCase);
			}
			else if (SortColumn == ColumnId_Asset)
			{
				Cmp = A->FullMaterialPath.Compare(B->FullMaterialPath, ESearchCase::IgnoreCase);
			}
			else if (SortColumn == ColumnId_B1)
			{
				Cmp = A->ShadersA - B->ShadersA;
			}
			else if (SortColumn == ColumnId_B2)
			{
				Cmp = A->ShadersB - B->ShadersB;
			}
			else if (SortColumn == ColumnId_Delta)
			{
				Cmp = A->Delta - B->Delta;
			}
			return bAscending ? (Cmp < 0) : (Cmp > 0);
		});
	}

	// Update summary text
	SummaryText->SetText(FText::Format(
		NSLOCTEXT("MaterialDiff", "SummaryWithStatus",
			"Rows: {0}  (New: {4}  Removed: {5}  Changed: {6}  Same: {7})"),
		FText::AsNumber(FilteredItems.Num()),
		FText::AsNumber(TotalShadersA),
		FText::AsNumber(TotalShadersB),
		FText::AsNumber(TotalDelta),
		FText::AsNumber(CountNew),
		FText::AsNumber(CountRemoved),
		FText::AsNumber(CountChanged),
		FText::AsNumber(CountSame)));

	// Update numeric column headers with totals
	HeaderB1Text->SetText(FText::Format(
		NSLOCTEXT("MaterialDiff", "HeaderB1", "Build One ({0})"),
		FText::AsNumber(TotalShadersA)));

	HeaderB2Text->SetText(FText::Format(
		NSLOCTEXT("MaterialDiff", "HeaderB2", "Build Two ({0})"),
		FText::AsNumber(TotalShadersB)));

	FString DeltaHeaderStr = (TotalDelta >= 0)
		? FString::Printf(TEXT("Delta (+%d)"), TotalDelta)
		: FString::Printf(TEXT("Delta (%d)"), TotalDelta);
	HeaderDeltaText->SetText(FText::FromString(DeltaHeaderStr));

	ListView->RequestListRefresh();
}

void SMaterialDiffTable::OnSortColumnHeader(EColumnSortPriority::Type Priority, const FName& Column, EColumnSortMode::Type NewSortMode)
{
	if (SortColumn == Column)
	{
		// Toggle: Ascending -> Descending -> None
		if (SortMode == EColumnSortMode::Ascending)
		{
			SortMode = EColumnSortMode::Descending;
		}
		else
		{
			SortMode = EColumnSortMode::None;
			SortColumn = NAME_None;
		}
	}
	else
	{
		SortColumn = Column;
		SortMode = EColumnSortMode::Ascending;
	}
	RefreshFilteredItems();
}

EColumnSortMode::Type SMaterialDiffTable::GetSortModeForColumn(FName Column) const
{
	if (Column == SortColumn)
	{
		return SortMode;
	}
	return EColumnSortMode::None;
}

void SMaterialDiffTable::ApplyRegressionPreset()
{
	bShowNew = true;
	bShowRemoved = false;
	bShowChanged = true;
	bShowSame = false;
	SortColumn = ColumnId_Delta;
	SortMode = EColumnSortMode::Descending;
	RefreshFilteredItems();
}

void SMaterialDiffTable::ExportFilteredItemsToCSV()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return;
	}

	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

	TArray<FString> OutFilePaths;
	DesktopPlatform->SaveFileDialog(
		ParentWindowHandle,
		TEXT("Export Material Diff to CSV"),
		FPaths::ProjectSavedDir(),
		TEXT("MaterialDiff.csv"),
		TEXT("CSV Files (*.csv)|*.csv"),
		0,
		OutFilePaths);

	if (OutFilePaths.Num() == 0)
	{
		return;
	}

	const FString& FilePath = OutFilePaths[0];

	auto StatusToString = [](EAssetDiffStatus InStatus) -> FString
	{
		switch (InStatus)
		{
		case EAssetDiffStatus::New:     return TEXT("New");
		case EAssetDiffStatus::Removed: return TEXT("Removed");
		case EAssetDiffStatus::Changed: return TEXT("Changed");
		case EAssetDiffStatus::Same:
		default:                        return TEXT("Same");
		}
	};

	FString CSV;
	CSV += TEXT("Status,Parent,Asset,Build One,Build Two,Delta\n");

	for (const FMaterialDiffNodePtr& Item : FilteredItems)
	{
		CSV += FString::Printf(TEXT("%s,\"%s\",\"%s\",%d,%d,%d\n"),
			*StatusToString(Item->Status),
			*Item->ParentMaterialPath,
			*Item->FullMaterialPath,
			Item->ShadersA,
			Item->ShadersB,
			Item->Delta);
	}

	FFileHelper::SaveStringToFile(CSV, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
