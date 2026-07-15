// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetaHumanCharacterEditorBlendToolPanel.h"

#include "Algo/Find.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "MetaHumanPaletteItemKey.h"
#include "Algo/RemoveIf.h"
#include "AssetThumbnail.h"
#include "AssetToolsModule.h"
#include "ContentBrowserItem.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterAssetObserver.h"
#include "MetaHumanCharacterEditorModule.h"
#include "MetaHumanCharacterEditorSettings.h"
#include "MetaHumanCharacterEditorStyle.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterEditorWardrobeSettings.h"
#include "Misc/ObjectThumbnail.inl"
#include "ObjectTools.h"
#include "Slate/DeferredCleanupSlateBrush.h"
#include "Styling/SlateBrush.h"
#include "Tools/MetaHumanCharacterEditorBodyEditingTools.h"
#include "Tools/MetaHumanCharacterEditorFaceEditingTools.h"
#include "UObject/SavePackage.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMetaHumanCharacterEditorBlendToolPanel"

void SMetaHumanCharacterEditorBlendToolThumbnail::Construct(const FArguments& InArgs, int32 InItemIndex)
{
	OnItemContentSetDelegate = InArgs._OnItemContentSet;
	OnItemDeletedDelegate = InArgs._OnItemDeleted;
	
	ItemIndex = InItemIndex;

	DefaultBrush = FMetaHumanCharacterEditorStyle::Get().GetBrush("MetaHumanCharacterEditorTools.Rounded.DefaultBrush");
	SelectedBrush = FMetaHumanCharacterEditorStyle::Get().GetBrush("MetaHumanCharacterEditorTools.Rounded.SelectedBrush");

	ThumbnailPool = MakeShared<FAssetThumbnailPool>(128);
	AssetThumbnail = MakeShared<FAssetThumbnail>(nullptr, 112.f, 112.f, ThumbnailPool);

	FAssetThumbnailConfig ThumbnailConfig;
	ThumbnailConfig.ThumbnailLabel = EThumbnailLabel::AssetName;

	ChildSlot
		[
			SNew(SBorder)
			.BorderImage(this, &SMetaHumanCharacterEditorBlendToolThumbnail::GetBorderBrush)
			.Padding(2.f)
			[
				SNew(SVerticalBox)

				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.HeightOverride(100.f)
					.WidthOverride(100.f)
					[
						SNew(SOverlay)

						// Thumbnail main section
						+SOverlay::Slot()
						[
							SAssignNew(ThumbnailContainerBox, SBox)
							[
								AssetThumbnail->MakeThumbnailWidget(ThumbnailConfig)
							]
						]
							
						// Thumbnail overlay section
						+ SOverlay::Slot()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Top)
						[
							SNew(SVerticalBox)

							// Thumbnail delete button section
							+SVerticalBox::Slot()
							.HAlign(HAlign_Right)
							.VAlign(VAlign_Top)
							.AutoHeight()
							[
								SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
								.OnClicked(this, &SMetaHumanCharacterEditorBlendToolThumbnail::OnDeleteButtonClicked)
								.Visibility(this, &SMetaHumanCharacterEditorBlendToolThumbnail::GetDeleteButtonVisibility)
								[
									SNew(SImage)
									.Image(FAppStyle::Get().GetBrush("Icons.X"))
								]
							]
						]
					]
				]

				// Thumbnail Label section
				+ SVerticalBox::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(0.f, 4.f, 0.f, 0.f)
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(this, &SMetaHumanCharacterEditorBlendToolThumbnail::GetThumbnailNameAsText)
					.Font(FAppStyle::GetFontStyle("ContentBrowser.AssetTileViewNameFont"))
					.OverflowPolicy(ETextOverflowPolicy::MultilineEllipsis)
				]
			]
		];
}

FAssetData SMetaHumanCharacterEditorBlendToolThumbnail::GetThumbnailAssetData() const
{
	FAssetData AssetData;
	if (AssetThumbnail.IsValid())
	{
		AssetData = AssetThumbnail->GetAssetData();
	}

	return AssetData;
}

void SMetaHumanCharacterEditorBlendToolThumbnail::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	bIsDragging = true;

	ChildSlot.SetPadding(FMargin(-2.f));
}

void SMetaHumanCharacterEditorBlendToolThumbnail::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	bIsDragging = false;

	ChildSlot.SetPadding(FMargin(0.f));
}

FReply SMetaHumanCharacterEditorBlendToolThumbnail::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	bIsDragging = false;

    TSharedPtr<FMetaHumanCharacterAssetViewItemDragDropOp> AssetDragDropOperation = DragDropEvent.GetOperationAs<FMetaHumanCharacterAssetViewItemDragDropOp>();
	if (!AssetDragDropOperation.IsValid() || !AssetThumbnail.IsValid() || !ThumbnailContainerBox.IsValid())
	{
		return FReply::Handled();
	}

	const TSharedPtr<FMetaHumanCharacterAssetViewItem>& AssetItem = AssetDragDropOperation->AssetItem;
	if (!AssetItem.IsValid())
	{
		return FReply::Handled();
	}

	SetContent(AssetItem);
	return FReply::Handled();
}

void SMetaHumanCharacterEditorBlendToolThumbnail::SetContent(const TSharedPtr<FMetaHumanCharacterAssetViewItem>& InAssetItem)
{
	if (!InAssetItem.IsValid() || !AssetThumbnail.IsValid() || !ThumbnailContainerBox.IsValid())
	{
		return;
	}

	const FAssetData& AssetData = InAssetItem->AssetData;
	AssetThumbnail->SetAsset(AssetData);
	if (InAssetItem->ThumbnailImageOverride.IsValid())
	{
		ThumbnailContainerBox->SetContent(GenerateThumbnailWidget(InAssetItem));
	}
	else
	{
		AssetThumbnail->SetRealTime(true);
		AssetThumbnail->RefreshThumbnail();

		ThumbnailContainerBox->SetContent(AssetThumbnail->MakeThumbnailWidget());
	}

	OnItemContentSetDelegate.ExecuteIfBound(AssetData, ItemIndex);
}

TSharedRef<SWidget> SMetaHumanCharacterEditorBlendToolThumbnail::GenerateThumbnailWidget(TSharedPtr<FMetaHumanCharacterAssetViewItem> AssetItem)
{
	TSharedPtr<SWidget> ThumbnailWidget =
		SNew(SImage)
		.Image(FAppStyle::GetDefaultBrush());

	if (!AssetItem.IsValid() || !AssetItem->ThumbnailImageOverride.IsValid())
	{
		return ThumbnailWidget.ToSharedRef();
	}

	const UObject* AssetItemObject = AssetItem->AssetData.GetAsset();

	if (IsValid(AssetItemObject))
	{
		const FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		const TSharedPtr<IAssetTypeActions> AssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(AssetItemObject->GetClass()).Pin();
		FLinearColor AssetColor = FLinearColor::White;
		if (AssetTypeActions.IsValid())
		{
			AssetColor = AssetTypeActions->GetTypeColor();
		}

		ThumbnailWidget =
			SNew(SOverlay)
			
			// Thumbnail image section
			+ SOverlay::Slot()
			[
				SNew(SImage)
				.Image_Lambda([AssetItem]() -> const FSlateBrush*
				{
					if (AssetItem.IsValid())
					{
						return FDeferredCleanupSlateBrush::TrySlateBrush(AssetItem->ThumbnailImageOverride);
					}

					return nullptr;
				})
			]

			// Color strip section
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Bottom)
			[
				SNew(SBox)
				.HeightOverride(2.f)
				.Padding(1.8f, 0.f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(AssetColor)
				]
			];
	}

	return ThumbnailWidget.ToSharedRef();
}

const FSlateBrush* SMetaHumanCharacterEditorBlendToolThumbnail::GetBorderBrush() const
{
	return IsHovered() || bIsDragging ? SelectedBrush : DefaultBrush;
}

FText SMetaHumanCharacterEditorBlendToolThumbnail::GetThumbnailNameAsText() const
{
	const FText InvalidNameText = LOCTEXT("BlendToolPanel_InvalidNameText", "None");

	if (AssetThumbnail.IsValid())
	{
		const FAssetData& AssetData = AssetThumbnail->GetAssetData();
		return IsValid(AssetData.GetAsset()) ? FText::FromName(AssetData.AssetName) : InvalidNameText;
	}

	return InvalidNameText;
}

EVisibility SMetaHumanCharacterEditorBlendToolThumbnail::GetDeleteButtonVisibility() const
{
	return AssetThumbnail.IsValid() && IsValid(AssetThumbnail->GetAsset()) ? EVisibility::Visible : EVisibility::Hidden;
}

FReply SMetaHumanCharacterEditorBlendToolThumbnail::OnDeleteButtonClicked()
{
	if (AssetThumbnail.IsValid())
	{
		AssetThumbnail->SetAsset(nullptr);
		AssetThumbnail->RefreshThumbnail();

		ThumbnailContainerBox->SetContent(AssetThumbnail->MakeThumbnailWidget());
	}
	OnItemDeletedDelegate.ExecuteIfBound(ItemIndex);
	return FReply::Handled();
}

void SMetaHumanCharacterEditorBlendToolPanel::Construct(const FArguments& InArgs, UMetaHumanCharacter* InCharacter, UMetaHumanCharacterEditorMeshBlendTool* InBlendTool)
{
	VirtualFolderSlotName = InArgs._VirtualFolderSlotName;
	OnItemActivatedDelegate = InArgs._OnItemActivated;
	OnOverrideItemThumbnailDelegate = InArgs._OnOverrideItemThumbnail;
	OnFilterAssetDataDelegate = InArgs._OnFilterAssetData;
	OnItemApplyHeadOnlyDelegate = InArgs._OnItemApplyHeadOnly;
	OnItemApplyBodyOnlyDelegate = InArgs._OnItemApplyBodyOnly;

	CharacterWeakPtr = InCharacter;
	BlendToolWeakPtr = InBlendTool;

	UMetaHumanCharacterEditorSettings* MetaHumanEditorSettings = GetMutableDefault<UMetaHumanCharacterEditorSettings>();
	if (!MetaHumanEditorSettings->GetOnPresetsDirectoriesChanged().IsBoundToObject(this))
	{
		MetaHumanEditorSettings->GetOnPresetsDirectoriesChanged().BindSP(this, &SMetaHumanCharacterEditorBlendToolPanel::OnPresetsDirectoriesChanged);
	}

	const auto CreateBlendToolThumbnail = [this, InArgs](int32 InItemIndex)
		{ 
			const TSharedRef<SMetaHumanCharacterEditorBlendToolThumbnail> BlendToolThumbnail =
				SNew(SMetaHumanCharacterEditorBlendToolThumbnail, InItemIndex)
				.OnItemContentSet(InArgs._OnItemContentSet)
				.OnItemDeleted(InArgs._OnItemDeleted)
				.ToolTipText(LOCTEXT("BlendToolThumbnailToolTip", "Drag up to three different MetaHuman presets here to be able to mix and match their features on your current MetaHuman."));

			BlendToolThumbnails.Add(BlendToolThumbnail);
			return BlendToolThumbnail;
		};

	ChildSlot
		[
			SNew(SVerticalBox)

			// Blend Tool Thumbnails section
			+ SVerticalBox::Slot()
			.Padding(0.f, 20.f)
			.AutoHeight()
			.HAlign(HAlign_Left)
			[
				SNew(SHorizontalBox)

				// Left side - Overlay with background and buttons
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SOverlay)

					// Background image section
					+ SOverlay::Slot()
					[
						SNew(SImage)
						.Image(FMetaHumanCharacterEditorStyle::Get().GetBrush(TEXT("MetaHumanCharacterEditorTools.BlendTool.Human")))
					]

					// Buttons section
					+ SOverlay::Slot()
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.Padding(0,7,0,0)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
							.OnClicked(this, &SMetaHumanCharacterEditorBlendToolPanel::OnCameraFocusButtonPressed, true)
							.ToolTipText(LOCTEXT("CameraFocusHeadButtonToolTip", "Click to focus the camera on the head."))
							[
								SNew(SImage)
								.Image(FMetaHumanCharacterEditorStyle::Get().GetBrush(TEXT("MetaHumanCharacterEditorTools.BlendTool.Camera")))
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.Padding(0,40,0,0)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
							.OnClicked(this, &SMetaHumanCharacterEditorBlendToolPanel::OnCameraFocusButtonPressed, false)
							.ToolTipText(LOCTEXT("CameraFocusBodyButtonToolTip", "Click to focus the camera on the body."))
							[
								SNew(SImage)
								.Image(FMetaHumanCharacterEditorStyle::Get().GetBrush(TEXT("MetaHumanCharacterEditorTools.BlendTool.Camera")))
							]
						]
					]
				]

				// Right side - Thumbnails section
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(20,10,0,10)
				[
					SNew(SVerticalBox)

					// Head thumbnails and circle
					+ SVerticalBox::Slot()
					[
						SNew(SOverlay)

						// Background image section
						+ SOverlay::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(SImage)
							.DesiredSizeOverride(FVector2D(250.f, 250.f))
							.Image(FMetaHumanCharacterEditorStyle::Get().GetBrush(TEXT("MetaHumanCharacterEditorTools.BlendTool.Circle")))
						]

						// Thumbnails section
						+ SOverlay::Slot()
						.Padding(4.f)
						[
							SNew(SConstraintCanvas)

							+ SConstraintCanvas::Slot()
							.Anchors(FAnchors(0.5f))
							.Offset(FMargin(0.f, -80.f))
							.AutoSize(true)
							[
								SNew(SBox)
								.WidthOverride(100.f)
								.HeightOverride(130.f)
								[
									CreateBlendToolThumbnail(0)
								]
							]

							+ SConstraintCanvas::Slot()
							.Anchors(FAnchors(0.5f))
							.Offset(FMargin(-90.f, 70.f))
							.AutoSize(true)
							[
								SNew(SBox)
								.WidthOverride(100.f)
								.HeightOverride(130.f)
								[
									CreateBlendToolThumbnail(1)
								]
							]

							+ SConstraintCanvas::Slot()
							.Anchors(FAnchors(0.5f))
							.Offset(FMargin(90.f, 70.f))
							.AutoSize(true)
							[
								SNew(SBox)
								.WidthOverride(100.f)
								.HeightOverride(130.f)
								[
									CreateBlendToolThumbnail(2)
								]
							]
						]
					]
				]
			]

			// Reset buttons
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.Padding(4.f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Button")
					.ForegroundColor(FLinearColor::White)
					.OnClicked(this, &SMetaHumanCharacterEditorBlendToolPanel::OnResetHeadButtonClicked)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.ToolTipText(LOCTEXT("BlendToolViewResetHeadToolTip", "Resets all head blend and sculpt parameters back to their default values, clearing any manual head customisation."))
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.f, 0.f, 4.f, 0.f)
						[
							SNew(SImage)
							.Image(FAppStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("BlendToolViewResetHeadText", "Reset Head"))
							.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
						]
					]
				]

				+ SHorizontalBox::Slot()
				.Padding(4.f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Button")
					.ForegroundColor(FLinearColor::White)
					.OnClicked(this, &SMetaHumanCharacterEditorBlendToolPanel::OnResetBodyButtonClicked)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.ToolTipText(LOCTEXT("BlendToolViewResetBodyToolTip", "Resets all body blend, sculpt and parameter settings back to their default values, clearing any manual body customisation."))
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.f, 0.f, 4.f, 0.f)
						[
							SNew(SImage)
							.Image(FAppStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("BlendToolViewResetBodyText", "Reset Body"))
							.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
						]
					]
				]
			]

			// New Preset button
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.f, 4.f, 4.f, 10.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "Button")
				.ForegroundColor(FLinearColor::White)
				.OnClicked(this, &SMetaHumanCharacterEditorBlendToolPanel::OnSavePresetButtonClicked)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.ToolTipText(LOCTEXT("BlendToolViewSavePresetToolTip", "Saves the current MetaHuman configuration as a named preset in your personal library, so it can be reused or blended from in future sessions."))
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 4.f, 0.f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.Plus"))
						.ColorAndOpacity(FStyleColors::AccentGreen)
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("BlendToolViewNewPresetText", "New Preset"))
						.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					]
				]
			]

			// Presets View section
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(AssetViewsPanel, SMetaHumanCharacterEditorAssetViewsPanel)
				.AllowDragging(true)
				.AllowSlots(false)
				.AllowMultiSelection(false)
				.AllowSlotMultiSelection(false)
				.ShowThumbnailType(true)
				.AssetViewSections(this, &SMetaHumanCharacterEditorBlendToolPanel::GetAssetViewsSections)
				.ExcludedObjects({ InCharacter })
				.VirtualFolderClassesToFilter({ UMetaHumanCharacter::StaticClass() })
				.OnPopulateAssetViewsItems(this, &SMetaHumanCharacterEditorBlendToolPanel::OnPopulateAssetViewsItems)
				.OnProcessDroppedFolders(this, &SMetaHumanCharacterEditorBlendToolPanel::OnProcessDroppedFolders)
				.OnItemDeleted(this, &SMetaHumanCharacterEditorBlendToolPanel::OnBlendToolVirtualItemDeleted)
				.CanDeleteItem(this, &SMetaHumanCharacterEditorBlendToolPanel::CanDeleteBlendToolVirtualItem)
				.OnFolderDeleted(this, &SMetaHumanCharacterEditorBlendToolPanel::OnPresetsPathsFolderDeleted)
				.CanDeleteFolder(this, &SMetaHumanCharacterEditorBlendToolPanel::CanDeletePresetsPathsFolder)
				.OnHadleVirtualItem(this, &SMetaHumanCharacterEditorBlendToolPanel::OnHandleBlendVirtualItem)
				.OnItemActivated(OnItemActivatedDelegate)
				.OnOverrideThumbnail(OnOverrideItemThumbnailDelegate)
				.OnItemApplyHeadOnly(OnItemApplyHeadOnlyDelegate)
				.OnItemApplyBodyOnly(OnItemApplyBodyOnlyDelegate)
			]
		];
}

TArray<FAssetData> SMetaHumanCharacterEditorBlendToolPanel::GetBlendableItems() const
{
	TArray<FAssetData> ItemsData;
	for (const TSharedPtr<SMetaHumanCharacterEditorBlendToolThumbnail>& BlendToolThumbnail : BlendToolThumbnails)
	{
		if (!BlendToolThumbnail.IsValid())
		{
			continue;
		}

		const FAssetData AssetData = BlendToolThumbnail->GetThumbnailAssetData();
		if (AssetData.IsValid())
		{
			ItemsData.Add(AssetData);
		}
	}

	return ItemsData;
}

void SMetaHumanCharacterEditorBlendToolPanel::RestorePresets(const TArray<TSoftObjectPtr<UMetaHumanCharacter>>& InPresets)
{
	const IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	for (int32 Index = 0; Index < InPresets.Num(); ++Index)
	{
		if (!BlendToolThumbnails.IsValidIndex(Index))
		{
			break;
		}

		const TSoftObjectPtr<UMetaHumanCharacter>& Preset = InPresets[Index];
		if (Preset.IsNull())
		{
			continue;
		}

		const FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(Preset.ToSoftObjectPath());
		if (!AssetData.IsValid())
		{
			continue;
		}

		TSharedPtr<FMetaHumanCharacterAssetViewItem> AssetItem = MakeShared<FMetaHumanCharacterAssetViewItem>(AssetData, NAME_None, FMetaHumanPaletteItemKey{}, nullptr, true);
		OnOverrideItemThumbnailDelegate.ExecuteIfBound(AssetItem);
		if (BlendToolThumbnails[Index].IsValid())
		{
			BlendToolThumbnails[Index]->SetContent(AssetItem);
		}
	}
}

TArray<FMetaHumanCharacterAssetViewItem> SMetaHumanCharacterEditorBlendToolPanel::GetCharacterIndividualAssets() const
{
	TArray<FMetaHumanCharacterAssetViewItem> Items;

	UMetaHumanCharacter* Character = CharacterWeakPtr.IsValid() ? CharacterWeakPtr.Get() : nullptr;
	if (!Character)
	{
		return Items;
	}

	const FMetaHumanCharacterIndividualAssets* IndividualAssets = Character->CharacterIndividualAssets.Find(VirtualFolderSlotName);
	if (!IndividualAssets)
	{
		return Items;
	}

	for (TSoftObjectPtr<UMetaHumanCharacter> Item : IndividualAssets->Characters)
	{
		if (!Item.IsValid())
		{
			continue;
		}

		const FAssetData AssetData = FAssetData(Item.Get());
		const bool bIsItemValid = true;
		const FMetaHumanCharacterAssetViewItem AssetItem(AssetData, NAME_None, FMetaHumanPaletteItemKey(), nullptr, bIsItemValid);
		Items.Add(AssetItem);
	}

	return Items;
}

TArray<FMetaHumanCharacterAssetsSection> SMetaHumanCharacterEditorBlendToolPanel::GetAssetViewsSections() const
{
	TArray<FMetaHumanCharacterAssetsSection> Sections;

	auto MakeSection = [](const FDirectoryPath& PathToMonitor)
	{
		const TArray<TSubclassOf<UObject>> ClassesToFiler = { UMetaHumanCharacter::StaticClass() };

		FMetaHumanCharacterAssetsSection Section;
		Section.ClassesToFilter = ClassesToFiler;
		Section.ContentDirectoryToMonitor = PathToMonitor;
		Section.SlotName = NAME_None;

		return Section;
	};

	// Append preset directories from the wardrobe settings
	if (FMetaHumanCharacterEditorModule::IsOptionalMetaHumanContentInstalled())
	{
		const UMetaHumanCharacterEditorWardrobeSettings* Settings = GetDefault<UMetaHumanCharacterEditorWardrobeSettings>();
		for (const FDirectoryPath& Path : Settings->PresetDirectories)
		{
			Sections.AddUnique(MakeSection(Path));
		}
	}

	// Append user sections from project settings
	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	for (const FDirectoryPath& Path : Settings->PresetsDirectories)
	{
		Sections.AddUnique(MakeSection(Path));
	}

	// Filter valid section settings
	return Sections.FilterByPredicate([](const FMetaHumanCharacterAssetsSection& Section)
		{
			FString LongPackageName;

			// Check if we provided the long package name
			if (!FPackageName::TryConvertLongPackageNameToFilename(
				Section.ContentDirectoryToMonitor.Path,
				LongPackageName))
			{
				return false;
			}

			if (Section.ClassesToFilter.IsEmpty())
			{
				return false;
			}

			return true;
		});
}

TArray<FMetaHumanCharacterAssetViewItem> SMetaHumanCharacterEditorBlendToolPanel::OnPopulateAssetViewsItems(const FMetaHumanCharacterAssetsSection& InSection, const FMetaHumanObserverChanges& InChanges)
{
	TArray<FMetaHumanCharacterAssetViewItem> Items;
 
	if (InSection.ContentDirectoryToMonitor.Path == TEXT("Individual Assets"))
	{
		Items.Append(GetCharacterIndividualAssets());
		return Items;
	}

	TArray<FAssetData> FoundAssets;
	FMetaHumanCharacterAssetObserver::Get().GetAssets(
		FName(InSection.ContentDirectoryToMonitor.Path),
		TSet(InSection.ClassesToFilter),
		FoundAssets);

	// Sort assets by name
	FoundAssets.Sort([](const FAssetData& AssetA, const FAssetData& AssetB)
		{
			return AssetA.AssetName.Compare(AssetB.AssetName) < 0;
		});

	for (const FAssetData& Asset : FoundAssets)
	{
		bool bFilterAsset = false;
		if (OnFilterAssetDataDelegate.IsBound())
		{
			bFilterAsset = OnFilterAssetDataDelegate.Execute(Asset);
		}

		if (!bFilterAsset)
		{
			const bool bIsItemValid = true;
			Items.Add(FMetaHumanCharacterAssetViewItem(Asset, InSection.SlotName, FMetaHumanPaletteItemKey(), nullptr, bIsItemValid));
		}
	}

	return Items;
}

void SMetaHumanCharacterEditorBlendToolPanel::OnProcessDroppedFolders(const TArray<FContentBrowserItem> Items, const FMetaHumanCharacterAssetsSection& InSection) const
{
	UMetaHumanCharacterEditorSettings* MetaHumanEditorSettings = GetMutableDefault<UMetaHumanCharacterEditorSettings>();
	if (!MetaHumanEditorSettings || Items.IsEmpty())
	{
		return;
	}

	for (const FContentBrowserItem& Item : Items)
	{
		if (!Item.IsFolder())
		{
			continue;
		}

		const FString Path = Item.GetInternalPath().ToString();
		const bool bAlreadyContinsPath =
			MetaHumanEditorSettings->PresetsDirectories.ContainsByPredicate(
				[Path, InSection](const FDirectoryPath& DirectoryPath)
				{
					return DirectoryPath.Path == Path;
				});

		if (!bAlreadyContinsPath)
		{
			FProperty* Property = UMetaHumanCharacterEditorSettings::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorSettings, PresetsDirectories));
			MetaHumanEditorSettings->PreEditChange(Property);

			MetaHumanEditorSettings->PresetsDirectories.Add(FDirectoryPath(Path));

			FPropertyChangedEvent PropertyChangedEvent(Property, EPropertyChangeType::ValueSet);
			MetaHumanEditorSettings->PostEditChangeProperty(PropertyChangedEvent);
		}
	}
}

void SMetaHumanCharacterEditorBlendToolPanel::OnBlendToolVirtualItemDeleted(TSharedPtr<FMetaHumanCharacterAssetViewItem> Item)
{
	UMetaHumanCharacter* Character = CharacterWeakPtr.IsValid() ? CharacterWeakPtr.Get() : nullptr;
	if (!AssetViewsPanel.IsValid() || !Character || !Item.IsValid())
	{
		return;
	}

	UMetaHumanCharacter* CharacterItem = Cast<UMetaHumanCharacter>(Item->AssetData.GetAsset());
	FMetaHumanCharacterIndividualAssets* IndividualAssets = Character->CharacterIndividualAssets.Find(VirtualFolderSlotName);
	if (!CharacterItem || !IndividualAssets)
	{
		return;
	}

	if (IndividualAssets->Characters.Contains(CharacterItem))
	{
		Character->Modify();
		IndividualAssets->Characters.Remove(TNotNull<UMetaHumanCharacter*>(CharacterItem));

		AssetViewsPanel->RequestRefresh();
	}
}

bool SMetaHumanCharacterEditorBlendToolPanel::CanDeleteBlendToolVirtualItem(TSharedPtr<FMetaHumanCharacterAssetViewItem> Item) const
{
	UMetaHumanCharacter* Character = CharacterWeakPtr.IsValid() ? CharacterWeakPtr.Get() : nullptr;
	if (!Character || !Item.IsValid() || !Item->AssetData.IsAssetLoaded())
	{
		return false;
	}

	UMetaHumanCharacter* CharacterItem = Cast<UMetaHumanCharacter>(Item->AssetData.GetAsset());
	FMetaHumanCharacterIndividualAssets* IndividualAssets = Character->CharacterIndividualAssets.Find(VirtualFolderSlotName);
	if (!CharacterItem || !IndividualAssets)
	{
		return false;
	}

	return IndividualAssets->Characters.Contains(CharacterItem);
}

void SMetaHumanCharacterEditorBlendToolPanel::OnPresetsPathsFolderDeleted(const FMetaHumanCharacterAssetsSection& InSection)
{
	UMetaHumanCharacterEditorSettings* MetaHumanEditorSettings = GetMutableDefault<UMetaHumanCharacterEditorSettings>();
	if (!MetaHumanEditorSettings)
	{
		return;
	}

	FProperty* Property = UMetaHumanCharacterEditorSettings::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorSettings, PresetsDirectories));
	MetaHumanEditorSettings->PreEditChange(Property);

	MetaHumanEditorSettings->PresetsDirectories.SetNum(Algo::RemoveIf(MetaHumanEditorSettings->PresetsDirectories,
		[InSection](const FDirectoryPath& DirectoryPath)
		{
			return DirectoryPath.Path == InSection.ContentDirectoryToMonitor.Path;
		}));

	FPropertyChangedEvent PropertyChangedEvent(Property, EPropertyChangeType::ValueSet);
	MetaHumanEditorSettings->PostEditChangeProperty(PropertyChangedEvent);
}

bool SMetaHumanCharacterEditorBlendToolPanel::CanDeletePresetsPathsFolder(TSharedPtr<FMetaHumanCharacterAssetViewItem> Item, const FMetaHumanCharacterAssetsSection& InSection) const
{
	UMetaHumanCharacterEditorSettings* MetaHumanEditorSettings = GetMutableDefault<UMetaHumanCharacterEditorSettings>();
	if (!MetaHumanEditorSettings)
	{
		return false;
	}

	return Algo::FindByPredicate(MetaHumanEditorSettings->PresetsDirectories,
		[InSection](const FDirectoryPath& DirectoryPath)
		{
			return DirectoryPath.Path == InSection.ContentDirectoryToMonitor.Path;
		}) != nullptr;
}

void SMetaHumanCharacterEditorBlendToolPanel::OnHandleBlendVirtualItem(TSharedPtr<FMetaHumanCharacterAssetViewItem> Item)
{
	UMetaHumanCharacter* Character = CharacterWeakPtr.IsValid() ? CharacterWeakPtr.Get() : nullptr;
	if (!Character || !Item.IsValid())
	{
		return;
	}

	UMetaHumanCharacter* CharacterItem = Cast<UMetaHumanCharacter>(Item->AssetData.GetAsset());
	if (!CharacterItem)
	{
		return;
	}

	FMetaHumanCharacterIndividualAssets& IndividualAssets = Character->CharacterIndividualAssets.FindOrAdd(VirtualFolderSlotName);
	if (!IndividualAssets.Characters.Contains(CharacterItem))
	{
		Character->Modify();
		IndividualAssets.Characters.Add(TNotNull<UMetaHumanCharacter*>(CharacterItem));
	}
}

void SMetaHumanCharacterEditorBlendToolPanel::OnPresetsDirectoriesChanged()
{
	if (AssetViewsPanel.IsValid())
	{
		AssetViewsPanel->RequestRefresh();
	}
}

FReply SMetaHumanCharacterEditorBlendToolPanel::OnCameraFocusButtonPressed(bool bShouldFocusFace)
{
	UMetaHumanCharacter* Character = CharacterWeakPtr.Get();
	if (!Character)
	{
		return FReply::Handled();
	}

	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterEditorSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	if (!MetaHumanCharacterEditorSubsystem)
	{
		return FReply::Handled();
	}

	const EMetaHumanCharacterCameraFrame FrameToFocus = bShouldFocusFace
		? EMetaHumanCharacterCameraFrame::Face
		: EMetaHumanCharacterCameraFrame::Body;

	MetaHumanCharacterEditorSubsystem->OnCameraFocusRequested(Character).ExecuteIfBound(Character, FrameToFocus);

	return FReply::Handled();
}

FReply SMetaHumanCharacterEditorBlendToolPanel::OnSavePresetButtonClicked()
{
	UMetaHumanCharacter* Character = CharacterWeakPtr.Get();
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	if (!Character || !Subsystem)
	{
		return FReply::Handled();
	}

	// Show save dialog so the user can choose location and name
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	FString OutPackageName;
	FString OutAssetName;
	AssetTools.CreateUniqueAssetName(
		FPackageName::GetLongPackagePath(Character->GetPackage()->GetPathName()) / Character->GetName(),
		TEXT(""), OutPackageName, OutAssetName);

	FSaveAssetDialogConfig SaveAssetDialogConfig;
	SaveAssetDialogConfig.DefaultPath = FPackageName::GetLongPackagePath(OutPackageName);
	SaveAssetDialogConfig.DefaultAssetName = OutAssetName;
	SaveAssetDialogConfig.AssetClassNames.Add(UMetaHumanCharacter::StaticClass()->GetClassPathName());
	SaveAssetDialogConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;
	SaveAssetDialogConfig.DialogTitleOverride = LOCTEXT("SavePresetTitle", "Save Blended Preset");

	const FString AssetPathAndName = IContentBrowserSingleton::Get().CreateModalSaveAssetDialog(SaveAssetDialogConfig);
	if (AssetPathAndName.IsEmpty())
	{
		return FReply::Handled();
	}

	const FString SaveAssetName = FPackageName::ObjectPathToPathWithinPackage(AssetPathAndName);
	const FString SaveAssetDir = FPackageName::GetLongPackagePath(AssetPathAndName);

	// Duplicate the current character asset to get an identical copy with all properties
	UObject* DuplicatedObject = AssetTools.DuplicateAsset(SaveAssetName, SaveAssetDir, Character);
	UMetaHumanCharacter* NewCharacter = Cast<UMetaHumanCharacter>(DuplicatedObject);
	if (!NewCharacter)
	{
		return FReply::Handled();
	}

	// Commit the current editing state (which includes any blending) into the new asset.
	// We serialize face and body states from the subsystem (which holds the live blended state)
	// and write them into the duplicated character, mirroring what CommitFaceState/CommitBodyState do.
	TSharedRef<const FMetaHumanCharacterIdentity::FState> FaceState = Subsystem->GetFaceState(Character);
	FSharedBuffer FaceStateData;
	FaceState->Serialize(FaceStateData);
	NewCharacter->SetFaceStateData(FaceStateData);
	NewCharacter->FaceEvaluationSettings.GlobalDelta = FaceState->GetSettings().GlobalVertexDeltaScale();
	NewCharacter->FaceEvaluationSettings.HighFrequencyDelta = FaceState->GetSettings().GlobalHighFrequencyScale();
	NewCharacter->FaceEvaluationSettings.HeadScale = FaceState->GetFaceScale();

	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> BodyState = Subsystem->GetBodyState(Character);
	FSharedBuffer BodyStateData;
	BodyState->Serialize(BodyStateData);
	NewCharacter->SetBodyStateData(BodyStateData);

	UPackage* NewPackage = NewCharacter->GetPackage();
	if (!NewPackage)
	{
		return FReply::Handled();
	}
	
	if (!Subsystem->TryAddObjectToEdit(NewCharacter))
	{
		return FReply::Handled();
	}
	ON_SCOPE_EXIT
	{
		Subsystem->RemoveObjectToEdit(NewCharacter);
	};

	// Save the new character's package to disk (removes asterisk, persists across restarts)
	{
		const FString PackageFilename = FPackageName::LongPackageNameToFilename(
			NewPackage->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(NewPackage, NewCharacter, *PackageFilename, SaveArgs);
	}
	
	// Add to "Individual Assets"
	FMetaHumanCharacterIndividualAssets& IndividualAssets =
	Character->CharacterIndividualAssets.FindOrAdd(VirtualFolderSlotName);
	if (!IndividualAssets.Characters.Contains(NewCharacter))
	{
		IndividualAssets.Characters.Insert(TNotNull<UMetaHumanCharacter*>(NewCharacter), 0);
		Character->Modify();
	}

	// Refresh the asset views panel to show individual assets on top
	if (AssetViewsPanel.IsValid())
	{
		FMetaHumanCharacterAssetViewsPanelStatus PanelStatus = AssetViewsPanel->GetAssetViewsPanelStatus();
		PanelStatus.bShowFolders = true;
		AssetViewsPanel->UpdateAssetViewsPanelStatus(PanelStatus);

		// Ensure the individual assets section is expanded so the newly saved preset is visible.
		// The section label is "Individual Assets" when folders are shown separately, or
		// "My Presets" when all sections are combined into a single view.
		TArray<FMetaHumanCharacterAssetViewStatus> ViewStatusArray = AssetViewsPanel->GetAssetViewsStatusArray();
		for (FMetaHumanCharacterAssetViewStatus& ViewStatus : ViewStatusArray)
		{
			if (ViewStatus.Label == TEXT("Individual Assets") || ViewStatus.Label == TEXT("My Presets"))
			{
				ViewStatus.bIsExpanded = true;
			}
		}
		AssetViewsPanel->UpdateAssetViewsStatus(ViewStatusArray);

		// Always force a rebuild so the new preset appears even when bShowFolders
		// was already true (UpdateAssetViewsPanelStatus only rebuilds on a change).
		AssetViewsPanel->RequestRefresh();
	}
	
	return FReply::Handled();
}

FReply SMetaHumanCharacterEditorBlendToolPanel::OnResetHeadButtonClicked()
{
	if (UMetaHumanCharacterEditorFaceTool* FaceTool = Cast<UMetaHumanCharacterEditorFaceTool>(BlendToolWeakPtr.Get()))
	{
		FaceTool->ResetFace();
	}
	return FReply::Handled();
}

FReply SMetaHumanCharacterEditorBlendToolPanel::OnResetBodyButtonClicked()
{
	const UMetaHumanCharacterEditorHeadAndBodyBlendTool* BodyBlendTool = Cast<UMetaHumanCharacterEditorHeadAndBodyBlendTool>(BlendToolWeakPtr.Get());
	if (IsValid(BodyBlendTool))
	{
		UMetaHumanCharacterEditorHeadAndBodyParameterProperties* HeadAndBodyParameterProperties = Cast<UMetaHumanCharacterEditorHeadAndBodyParameterProperties>(BodyBlendTool->GetHeadAndBodyParameterProperties());
		HeadAndBodyParameterProperties->ResetBody();
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
