// Copyright Epic Games, Inc. All Rights Reserved.
#include "SAssetGroupItemView.h"

#include "MetaHumanAssetReport.h"
#include "ProjectUtilities/MetaHumanAssetManager.h"
#include "SMetaHumanAssetReportView.h"
#include "UI/MetaHumanStyleSet.h"

#include "AssetDefinition.h"
#include "AssetDefinitionRegistry.h"
#include "Application/SlateApplicationBase.h"
#include "AssetThumbnail.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Components/VerticalBox.h"
#include "HAL/FileManager.h"
#include "Misc/ScopedSlowTask.h"
#include "Styling/StyleColors.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "AssetGroupItemView"

namespace UE::MetaHuman
{

// Customized container for asset thumbnail widgets. Adds border and minimize / maximize button.
class SAssetGroupItemPreview : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SAssetGroupItemPreview)
		{
		}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_EVENT(FSimpleDelegate, OnChangeMaximized)
		SLATE_ARGUMENT(bool, IsMaximized)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		const TSharedRef<SWidget>& AssetThumbnail = InArgs._Content.Widget;
		ChildSlot
		[
			// The actual light border
			SNew(SBorder)
			.BorderImage(FMetaHumanStyleSet::Get().GetBrush("ItemDetails.ThumbnailBorder"))
			[
				// AssetWidgets don't have a background, so use another border to set the background color
				SNew(SBorder)
				.BorderImage(FMetaHumanStyleSet::Get().GetBrush("ItemDetails.ThumbnailInnerBorder"))
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						AssetThumbnail
					]
					+ SOverlay::Slot()
					[
						SNew(SBox)
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Bottom)
						.Padding(FMetaHumanStyleSet::Get().GetFloat("ItemDetails.ResizeButtonMargin"))
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleRoundButton")
							.ButtonColorAndOpacity(FStyleColors::Panel)
							.ContentPadding(FMetaHumanStyleSet::Get().GetFloat("ItemDetails.ResizeButtonPadding"))
							.OnPressed(InArgs._OnChangeMaximized)
							[
								SNew(SImage)
								.Image(FMetaHumanStyleSet::Get().GetBrush(InArgs._IsMaximized ? "ItemDetails.MinimizeIcon" : "ItemDetails.MaximizeIcon"))
							]
						]
					]
				]
			]
		];
	}
};

// The display information about an asset that is part of the AssetGroup
class FAssetDetails
{
public:
	explicit FAssetDetails(const FName& InPackageName)
	{
		PackageName = InPackageName;
		Name = FPaths::GetBaseFilename(InPackageName.ToString());

		// We want the size on disk for the asset size
		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName.ToString(), FPackageName::GetAssetPackageExtension());
		Size = IFileManager::Get().FileSize(*Filename);

		TArray<FAssetData> PackagedAssets;
		IAssetRegistry::GetChecked().GetAssetsByPackageName(PackageName, PackagedAssets);
		if (PackagedAssets.Num()) // If somehow we have an empty package in the list, then it will show as type "Unknown"
		{
			if (const UAssetDefinitionRegistry* AssetDefinitionRegistry = UAssetDefinitionRegistry::Get())
			{
				if (const UAssetDefinition* AssetDefinition = AssetDefinitionRegistry->GetAssetDefinitionForAsset(PackagedAssets[0]))
				{
					Type = AssetDefinition->GetAssetDisplayName();
					TypeColor = AssetDefinition->GetAssetColor();
				}
			}
		}
	}

	FName PackageName;
	FString Name;
	FText Type = LOCTEXT("UnknownType", "Unknown");
	FLinearColor TypeColor = FLinearColor::White;
	int64 Size = 0;
};

// Represents a row in the details table
class SAssetDetailsRow : public SMultiColumnTableRow<TSharedPtr<FAssetDetails>>
{
public:
	SLATE_BEGIN_ARGS(SAssetDetailsRow)
		{
		}
		SLATE_ARGUMENT(TSharedPtr<FAssetDetails>, Item)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& OwnerTableView)
	{
		RowData = Args._Item;

		FSuperRowType::Construct(
			FSuperRowType::FArguments()
			.Padding(FMetaHumanStyleSet::Get().GetFloat("ItemDetails.DetailRowPadding")),
			OwnerTableView
		);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == NameColumn)
		{
			return SNew(SBox)
				.Padding(FMetaHumanStyleSet::Get().GetMargin("ItemDetails.DetailColumnMargin"))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(FMetaHumanStyleSet::Get().GetMargin("MetaHumanManager.IconMargin"))
					.AutoWidth()
					[
						SNew(SImage)
						.Image(FMetaHumanStyleSet::Get().GetBrush("ItemDetails.DetailFileIcon"))
						.ColorAndOpacity(RowData->TypeColor)
					]
					+ SHorizontalBox::Slot()
					.FillContentWidth(1)
					[
						SNew(STextBlock)
						.Font(FMetaHumanStyleSet::Get().GetFontStyle("ItemDetails.DetailEntryFont"))
						.Text(FText::FromString(RowData->Name))
					]
				];
		}
		if (ColumnName == TypeColumn)
		{
			return SNew(STextBlock)
				.Margin(FMetaHumanStyleSet::Get().GetMargin("ItemDetails.DetailColumnMargin"))
				.Font(FMetaHumanStyleSet::Get().GetFontStyle("ItemDetails.DetailEntryFont"))
				.Text(RowData->Type);
		}
		if (ColumnName == SizeColumn)
		{
			return SNew(STextBlock)
				.Margin(FMetaHumanStyleSet::Get().GetMargin("ItemDetails.DetailColumnMargin"))
				.Font(FMetaHumanStyleSet::Get().GetFontStyle("ItemDetails.DetailEntryFont"))
				.Text(FText::AsMemory(RowData->Size));
		}

		return SNullWidget::NullWidget;
	}

	static const FName NameColumn;
	static const FName TypeColumn;
	static const FName SizeColumn;

private:
	TSharedPtr<FAssetDetails> RowData;
};

const FName SAssetDetailsRow::NameColumn = "Name";
const FName SAssetDetailsRow::TypeColumn = "Type";
const FName SAssetDetailsRow::SizeColumn = "Size";

// Handles the display of the asset preview and details
class SAssetGroupItemDetails : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SAssetGroupItemDetails)
		{
		}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		const float SmallThumbnailSize = FMetaHumanStyleSet::Get().GetFloat("ItemDetails.SmallThumbnailSize");
		const float LargeThumbnailSize = FMetaHumanStyleSet::Get().GetFloat("ItemDetails.LargeThumbnailSize");

		// Create the thumbnails for the asset preview pane
		FAssetThumbnailConfig Config;
		Config.ShowAssetColor = false;
		ThumbnailPool = MakeShared<FAssetThumbnailPool>(256);
		AssetThumbnail = MakeShared<FAssetThumbnail>(FAssetData(), SmallThumbnailSize, SmallThumbnailSize, ThumbnailPool);
		LargeAssetThumbnail = MakeShared<FAssetThumbnail>(FAssetData(), LargeThumbnailSize, LargeThumbnailSize, ThumbnailPool);

		ChildSlot
		[
			SNew(SWidgetSwitcher)
			.WidgetIndex(this, &SAssetGroupItemDetails::GetPreviewSwitcherIndex)
			+ SWidgetSwitcher::Slot()
			[
				SNew(SAssetGroupItemPreview)
				.OnChangeMaximized(this, &SAssetGroupItemDetails::ToggleMaximizePreview)
				.IsMaximized(true)
				[
					LargeAssetThumbnail->MakeThumbnailWidget(Config)
				]
			]
			+ SWidgetSwitcher::Slot()
			[
				SNew(SVerticalBox)
				// The Asset Preview
				+ SVerticalBox::Slot()
				.MinHeight(SmallThumbnailSize)
				.MaxHeight(SmallThumbnailSize)
				.Padding(FMetaHumanStyleSet::Get().GetMargin("ItemDetails.DetailsSectionMargin"))
				[
					SNew(SAssetGroupItemPreview)
					.OnChangeMaximized(this, &SAssetGroupItemDetails::ToggleMaximizePreview)
					.IsMaximized(false)
					[
						AssetThumbnail->MakeThumbnailWidget(Config)
					]
				]
				// Asset Title
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMetaHumanStyleSet::Get().GetMargin("ItemDetails.DetailsSectionMargin"))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.Padding(FMetaHumanStyleSet::Get().GetMargin("ItemDetails.TitleTextMargin"))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.AutoWidth()
						.Padding(FMetaHumanStyleSet::Get().GetMargin("ItemDetails.TitleIconMargin"))
						[
							SNew(SImage)
							.Image(this, &SAssetGroupItemDetails::GetItemAssetTypeIcon)
						]
						+ SHorizontalBox::Slot()
						.FillContentWidth(1)
						[
							SNew(STextBlock)
							.Font(FMetaHumanStyleSet::Get().GetFontStyle("ItemDetails.TitleFont"))
							.Text(this, &SAssetGroupItemDetails::GetItemName)
						]
					]
					+ SVerticalBox::Slot()
					.Padding(FMetaHumanStyleSet::Get().GetMargin("ItemDetails.TitleTextMargin"))
					[
						SNew(STextBlock)
						.Font(FMetaHumanStyleSet::Get().GetFontStyle("ItemDetails.DetailsTextFont"))
						.Text(this, &SAssetGroupItemDetails::GetItemAssetTypeName)
					]
					// Multi-select subtitle — only visible when other items are also selected
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMetaHumanStyleSet::Get().GetMargin("ItemDetails.TitleTextMargin"))
					[
						SNew(STextBlock)
						.Font(FMetaHumanStyleSet::Get().GetFontStyle("ItemDetails.DetailsTextFont"))
						.Text(this, &SAssetGroupItemDetails::GetOtherSelectedText)
						.ColorAndOpacity(FStyleColors::AccentGreen)
						.Visibility(this, &SAssetGroupItemDetails::GetOtherSelectedVisibility)
					]
				]
				// Verification report
				+ SVerticalBox::Slot()
				.AutoHeight()
				.MaxHeight(200.f)
				.Padding(FMetaHumanStyleSet::Get().GetMargin("ItemDetails.DetailsSectionMargin"))
				[
					SAssignNew(ReportView, SMetaHumanAssetReportView)
				]
				// Sub-asset exclusion checkboxes (populated dynamically in SetItem)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMetaHumanStyleSet::Get().GetMargin("ItemDetails.DetailsSectionMargin"))
				[
					SAssignNew(ExclusionPanel, SVerticalBox)
				]
				// Asset Details section
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMetaHumanStyleSet::Get().GetMargin("ItemDetails.DetailsSectionMargin"))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.Padding(FMetaHumanStyleSet::Get().GetMargin("ItemDetails.DetailsTextMargin"))
					[
						SNew(STextBlock)
						.Font(FMetaHumanStyleSet::Get().GetFontStyle("ItemDetails.DetailsEmphasisFont"))
						.Text(LOCTEXT("AssetDetailsTitle", "Asset Details"))
						.ColorAndOpacity(FStyleColors::White)
					]
					+ SVerticalBox::Slot()
					.Padding(FMetaHumanStyleSet::Get().GetMargin("ItemDetails.DetailsTextMargin"))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Font(FMetaHumanStyleSet::Get().GetFontStyle("ItemDetails.DetailsTextFont"))
							.Text(LOCTEXT("TotalSizeHeading", "Total Size:"))
						]
						+ SHorizontalBox::Slot()
						.FillContentWidth(1)
						[
							SNew(STextBlock)
							.Font(FMetaHumanStyleSet::Get().GetFontStyle("ItemDetails.DetailsEmphasisFont"))
							.Text(this, &SAssetGroupItemDetails::GetItemTotalSize)
						]
					]
					+ SVerticalBox::Slot()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Font(FMetaHumanStyleSet::Get().GetFontStyle("ItemDetails.DetailsTextFont"))
							.Text(LOCTEXT("NumAssetsHeading", "Number of referenced assets:"))
						]
						+ SHorizontalBox::Slot()
						.FillContentWidth(1)
						[
							SNew(STextBlock)
							.Font(FMetaHumanStyleSet::Get().GetFontStyle("ItemDetails.DetailsEmphasisFont"))
							.Text(this, &SAssetGroupItemDetails::GetItemNumAssets)
						]
					]
				]
				+ SVerticalBox::Slot()
				.FillContentHeight(1)
				[
					SAssignNew(ListView, SListView<TSharedPtr<FAssetDetails>>)
					.ListViewStyle(&FMetaHumanStyleSet::Get().GetWidgetStyle<FTableViewStyle>("MetaHumanManager.ListViewStyle"))
					.HeaderRow(SNew(SHeaderRow)
						.Style(&FMetaHumanStyleSet::Get().GetWidgetStyle<FHeaderRowStyle>("MetaHumanManager.ListHeaderRowStyle"))
						+ SHeaderRow::Column(SAssetDetailsRow::NameColumn).DefaultLabel(LOCTEXT("AssetNameHeader", "Name")).FillWidth(1.0f)
						+ SHeaderRow::Column(SAssetDetailsRow::TypeColumn).DefaultLabel(LOCTEXT("AssetTypeHeader", "Type")).FillWidth(0.6f)
						+ SHeaderRow::Column(SAssetDetailsRow::SizeColumn).DefaultLabel(LOCTEXT("AssetSizeHeader", "Disk Size")).FillWidth(0.6f)
					)
					.ListItemsSource(&AssetDetails)
					.OnGenerateRow(this, &SAssetGroupItemDetails::GetDetailsRowForItem)
					.OnMouseButtonDoubleClick_Lambda([](TSharedPtr<FAssetDetails> Item)
					{
						if (Item.IsValid())
						{
							TArray<FAssetData> Assets;
							IAssetRegistry::GetChecked().GetAssetsByPackageName(Item->PackageName, Assets);
							if (!Assets.IsEmpty())
							{
								FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
								ContentBrowserModule.Get().SyncBrowserToAssets({Assets[0]});
							}
						}
					})
				]
			]
		];
	}

	TSharedPtr<FMetaHumanAssetDescription> GetItem()
	{
		return CurrentAssetGroup;
	}

	void SetItem(const TSharedPtr<FMetaHumanAssetDescription> AssetDescription, int32 InOtherSelectedCount)
	{
		if (CurrentAssetGroup == AssetDescription)
		{
			if (CurrentAssetGroup.IsValid())
			{
				// Re-selecting the same instance — update the report and subtitle if required.
				ReportView->SetReport(CurrentAssetGroup->VerificationReport);
			}
			OtherSelectedCount = InOtherSelectedCount;
			return;
		}

		CurrentAssetGroup = AssetDescription;
		OtherSelectedCount = InOtherSelectedCount;
		bIsPreviewMaximized = false;
		AssetDetails.Reset();

		if (CurrentAssetGroup.IsValid())
		{
			// Ensure that all asset info is up to date
			FScopedSlowTask LoadingTask(1, LOCTEXT("UpdatingAssetTask", "Updating asset details..."));
			constexpr float Delay = 2.0f;
			LoadingTask.MakeDialogDelayed(Delay);
			LoadingTask.EnterProgressFrame();
			UMetaHumanAssetManager::UpdateAssetDependencies(*AssetDescription);

			AssetThumbnail->SetAsset(CurrentAssetGroup->AssetData);
			LargeAssetThumbnail->SetAsset(CurrentAssetGroup->AssetData);
			ReportView->SetReport(CurrentAssetGroup->VerificationReport);

			// Populate sub-asset exclusion checkboxes.
			ExclusionPanel->ClearChildren();
			TArray<FMetaHumanExcludableSubAsset> Excludables = UMetaHumanAssetManager::FindExcludableSubAssets(*AssetDescription);
			if (!Excludables.IsEmpty())
			{
				ExclusionPanel->AddSlot()
				.AutoHeight()
				.Padding(FMetaHumanStyleSet::Get().GetMargin("ItemDetails.DetailsTextMargin"))
				[
					SNew(STextBlock)
					.Font(FMetaHumanStyleSet::Get().GetFontStyle("ItemDetails.DetailsEmphasisFont"))
					.Text(LOCTEXT("SubAssetExclusionTitle", "Include in Package"))
					.ColorAndOpacity(FStyleColors::White)
				];

				TSharedPtr<FMetaHumanAssetDescription> AssetPtr = AssetDescription;
				for (const FMetaHumanExcludableSubAsset& SubAsset : Excludables)
				{
					const FName SubAssetRoot = SubAsset.RootPackage;
					const bool bCurrentlyExcluded = AssetDescription->ExcludedSubAssetRoots.Contains(SubAssetRoot);

					FText TypeLabel;
					switch (SubAsset.AssetType)
					{
					case EMetaHumanAssetType::Groom: TypeLabel = LOCTEXT("ExclGroomType", "Groom"); break;
					case EMetaHumanAssetType::SkeletalClothing: TypeLabel = LOCTEXT("ExclSkeletalType", "Skeletal Clothing"); break;
					case EMetaHumanAssetType::OutfitClothing: TypeLabel = LOCTEXT("ExclOutfitType", "Outfit Clothing"); break;
					default: TypeLabel = LOCTEXT("ExclUnknownType", "Asset"); break;
					}

					const FText Label = FText::Format(
						LOCTEXT("SubAssetCheckboxFmt", "{0}: {1}"),
						TypeLabel,
						FText::FromName(SubAsset.Name));

					ExclusionPanel->AddSlot()
					.AutoHeight()
					.Padding(4.0f, 2.0f)
					[
						SNew(SCheckBox)
						.IsChecked(bCurrentlyExcluded ? ECheckBoxState::Unchecked : ECheckBoxState::Checked)
						.OnCheckStateChanged_Lambda([AssetPtr, SubAssetRoot, this](ECheckBoxState NewState)
						{
							if (AssetPtr.IsValid())
							{
								if (NewState == ECheckBoxState::Checked)
								{
									AssetPtr->ExcludedSubAssetRoots.Remove(SubAssetRoot);
								}
								else
								{
									AssetPtr->ExcludedSubAssetRoots.Add(SubAssetRoot);
								}

								// Re-walk dependencies and rebuild the details list.
								UMetaHumanAssetManager::UpdateAssetDependencies(*AssetPtr);
								AssetDetails.Reset();
								for (const FName& Package : AssetPtr->DependentPackages)
								{
									AssetDetails.Emplace(MakeShared<FAssetDetails>(Package));
								}
								ListView->RebuildList();
							}
						})
						[
							SNew(STextBlock)
							.Text(Label)
						]
					];
				}
			}

			for (const FName& Package : AssetDescription->DependentPackages)
			{
				AssetDetails.Emplace(MakeShared<FAssetDetails>(Package));
			}

			// Sort, first by type, then by asset name
			AssetDetails.Sort([](const TSharedPtr<FAssetDetails>& A, const TSharedPtr<FAssetDetails>& B)
			{
				// We can assume types have consistent capitalisation
				const int Comp = A->Type.ToString().Compare(B->Type.ToString());
				if (Comp == 0)
				{
					return A->Name.ToLower().Compare(B->Name.ToLower()) < 0;
				}
				return Comp < 0;
			});
		}
		else
		{
			AssetThumbnail->SetAsset({});
			LargeAssetThumbnail->SetAsset({});
			ReportView->SetReport(nullptr);
			ExclusionPanel->ClearChildren();
		}
		ListView->RebuildList();
	}

private:
	TSharedRef<ITableRow> GetDetailsRowForItem(TSharedPtr<FAssetDetails> DetailsItem, const TSharedRef<STableViewBase>& Owner)
	{
		return SNew(SAssetDetailsRow, Owner)
			.Item(DetailsItem);
	}

	int32 GetPreviewSwitcherIndex() const
	{
		return bIsPreviewMaximized ? 0 : 1;
	}

	FText GetItemName() const
	{
		if (CurrentAssetGroup.IsValid())
		{
			return FText::FromName(CurrentAssetGroup->Name);
		}
		return LOCTEXT("NoNameAvailable", "None");
	}

	FText GetItemAssetTypeName() const
	{
		if (CurrentAssetGroup.IsValid())
		{
			if (CurrentAssetGroup->AssetType == EMetaHumanAssetType::Groom)
			{
				return LOCTEXT("GroomAssetType", "Groom");
			}
			if (CurrentAssetGroup->AssetType == EMetaHumanAssetType::SkeletalClothing)
			{
				return LOCTEXT("SkeletalClothingAssetType", "Skeletal Clothing");
			}
			if (CurrentAssetGroup->AssetType == EMetaHumanAssetType::OutfitClothing)
			{
				return LOCTEXT("OutfitClothingAssetType", "Outfit");
			}
			if (CurrentAssetGroup->AssetType == EMetaHumanAssetType::CharacterAssembly)
			{
				return LOCTEXT("CharacterAssemblyAssetType", "MetaHuman Assembly");
			}
			if (CurrentAssetGroup->AssetType == EMetaHumanAssetType::Character)
			{
				return LOCTEXT("CharacterAssetType", "MetaHuman Character");
			}
		}
		return LOCTEXT("UnknownAssetType", "Unknown");
	}

	FText GetOtherSelectedText() const
	{
		return FText::Format(
			LOCTEXT("OtherSelectedFmt", "(+ {0} {0}|plural(one=other,other=others) selected)"),
			OtherSelectedCount);
	}

	EVisibility GetOtherSelectedVisibility() const
	{
		return OtherSelectedCount > 0 ? EVisibility::Visible : EVisibility::Collapsed;
	}

	FText GetItemTotalSize() const
	{
		const int Size = CurrentAssetGroup.IsValid() ? CurrentAssetGroup->TotalSize : 0;
		return FText::AsMemory(Size);
	}

	FText GetItemNumAssets() const
	{
		const int NumAssets = CurrentAssetGroup.IsValid() ? CurrentAssetGroup->DependentPackages.Num() : 0;
		return FText::AsNumber(NumAssets);
	}

	const FSlateBrush* GetItemAssetTypeIcon() const
	{
		if (CurrentAssetGroup.IsValid())
		{
			if (CurrentAssetGroup->AssetType == EMetaHumanAssetType::Groom)
			{
				return FMetaHumanStyleSet::Get().GetBrush("ItemDetails.GroomIcon");
			}
			if (CurrentAssetGroup->AssetType == EMetaHumanAssetType::SkeletalClothing
				|| CurrentAssetGroup->AssetType == EMetaHumanAssetType::OutfitClothing)
			{
				return FMetaHumanStyleSet::Get().GetBrush("ItemDetails.ClothingIcon");
			}
			if (CurrentAssetGroup->AssetType == EMetaHumanAssetType::CharacterAssembly
				|| CurrentAssetGroup->AssetType == EMetaHumanAssetType::Character)
			{
				return FMetaHumanStyleSet::Get().GetBrush("ItemDetails.CharacterIcon");
			}
		}
		return FMetaHumanStyleSet::Get().GetBrush("ItemDetails.DefaultIcon");
	}

	void ToggleMaximizePreview()
	{
		bIsPreviewMaximized = !bIsPreviewMaximized;
	}

	// UI Elements
	TSharedPtr<SListView<TSharedPtr<FAssetDetails>>> ListView;
	TSharedPtr<SVerticalBox> ExclusionPanel;

	// Thumbnail handling
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	TSharedPtr<FAssetThumbnail> AssetThumbnail;
	TSharedPtr<FAssetThumbnail> LargeAssetThumbnail;
	TSharedPtr<SMetaHumanAssetReportView> ReportView;

	// Data
	TSharedPtr<FMetaHumanAssetDescription> CurrentAssetGroup;
	TArray<TSharedPtr<FAssetDetails>> AssetDetails;
	bool bIsPreviewMaximized = false;

	/** Number of other items checked alongside the current item. 0 = this is the only selection. */
	int32 OtherSelectedCount = 0;
};


void SAssetGroupItemView::Construct(const FArguments& InArgs)
{
	OnVerifyCallback = InArgs._OnVerify;
	OnPackageCallback = InArgs._OnPackage;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FMetaHumanStyleSet::Get().GetBrush("MetaHumanManager.RoundedBorder"))
		.Padding(FMetaHumanStyleSet::Get().GetMargin("ItemDetails.Padding"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillContentHeight(1)
			[
				SAssignNew(ItemDetails, SAssetGroupItemDetails)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1)
				.Padding(FMetaHumanStyleSet::Get().GetMargin("ItemDetails.PackageButtonPadding"))
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
					.Text(InArgs._PackageButtonText)
					.IsEnabled(InArgs._EnablePackageButton)
					.OnClicked(FOnClicked::CreateSP(this, &SAssetGroupItemView::OnPackage))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1)
				.Padding(FMetaHumanStyleSet::Get().GetMargin("ItemDetails.VerifyButtonPadding"))
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(InArgs._VerifyButtonText)
					.IsEnabled(this, &SAssetGroupItemView::EnableVerifyButton)
					.OnClicked(FOnClicked::CreateSP(this, &SAssetGroupItemView::OnVerify))
				]
			]
		]
	];
}

void SAssetGroupItemView::SetItem(TSharedPtr<FMetaHumanAssetDescription> AssetDescription, int32 OtherSelectedCount)
{
	ItemDetails->SetItem(AssetDescription, OtherSelectedCount);
}

FReply SAssetGroupItemView::OnVerify() const
{
	OnVerifyCallback.ExecuteIfBound();
	return FReply::Handled();
}

FReply SAssetGroupItemView::OnPackage() const
{
	OnPackageCallback.ExecuteIfBound();
	return FReply::Handled();
}

bool SAssetGroupItemView::EnableVerifyButton() const
{
	return ItemDetails->GetItem().IsValid();
}

} // namespace UE::MetaHuman

#undef LOCTEXT_NAMESPACE
