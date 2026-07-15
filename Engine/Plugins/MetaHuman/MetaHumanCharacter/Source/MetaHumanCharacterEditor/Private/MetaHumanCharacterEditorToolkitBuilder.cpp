// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorToolkitBuilder.h"

#include "FToolkitWidgetStyle.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Framework/Commands/UICommandList.h"
#include "IDetailsView.h"
#include "Layout/SeparatorTemplates.h"
#include "Styling/AppStyle.h"
#include "Styling/ToolBarStyle.h"
#include "Textures/SlateIcon.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditor"

namespace UE::MetaHumanCharacterEditor::Private
{
	constexpr float CategoryColumnWidth = 72.f;
	constexpr float CategoryLabelWrapAt = 56.f;
}

FMetaHumanCharacterEditorToolkitBuilder::FMetaHumanCharacterEditorToolkitBuilder(
	FName ToolbarCustomizationName,
	TSharedPtr<FUICommandList> InToolkitCommandList,
	TSharedPtr<FToolkitSections> InToolkitSections)
	: FToolkitBuilder(ToolbarCustomizationName, InToolkitCommandList, InToolkitSections)
{
	const TSharedPtr<FMetaHumanCharacterEditorToolkitSections> Sections = StaticCastSharedPtr<FMetaHumanCharacterEditorToolkitSections>(InToolkitSections);
	if (Sections.IsValid())
	{
		ToolCustomWarningsArea = Sections->ToolCustomWarningsArea;
		ToolViewArea = Sections->ToolViewArea;
	}
}

FMetaHumanCharacterEditorToolkitBuilder::FMetaHumanCharacterEditorToolkitBuilder(FToolkitBuilderArgs& Args)
	: FToolkitBuilder(Args)
{
	const TSharedPtr<FMetaHumanCharacterEditorToolkitSections> Sections = StaticCastSharedPtr<FMetaHumanCharacterEditorToolkitSections>(Args.ToolkitSections);
	if (Sections.IsValid())
	{
		ToolCustomWarningsArea = Sections->ToolCustomWarningsArea;
		ToolViewArea = Sections->ToolViewArea;
	}
}

void FMetaHumanCharacterEditorToolkitBuilder::AddPaletteCustom(TSharedPtr<FToolPalette> Palette)
{
	if (Palette.IsValid() && Palette->LoadToolPaletteAction.IsValid())
	{
		OrderedLoadCommandNames.AddUnique(Palette->LoadToolPaletteAction->GetCommandName());
	}
	FToolkitBuilder::AddPalette(Palette);
}

void FMetaHumanCharacterEditorToolkitBuilder::AddPaletteCustom(TSharedPtr<FEditablePalette> Palette)
{
	if (Palette.IsValid() && Palette->LoadToolPaletteAction.IsValid())
	{
		OrderedLoadCommandNames.AddUnique(Palette->LoadToolPaletteAction->GetCommandName());
	}
	FToolkitBuilder::AddPalette(Palette);
}

TSharedRef<SWidget> FMetaHumanCharacterEditorToolkitBuilder::BuildCategoryColumnWidget()
{
	using namespace UE::MetaHumanCharacterEditor::Private;

	// Mirror FCategoryDrivenContentBuilderBase::GetCategoryToolBarStyleName, which is
	// not WIDGETREGISTRATION_API-exported and therefore not linkable from outside the module.
	const FName StyleName = CategoryButtonLabelVisibility.IsVisible() ?
		FName(TEXT("CategoryDrivenContentBuilderToolbarWithLabels")) :
		FName(TEXT("CategoryDrivenContentBuilderToolbarWithoutLabels"));
	const FToolBarStyle& ToolBarStyle = FAppStyle::Get().GetWidgetStyle<FToolBarStyle>(StyleName);

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);

	for (const FName& CommandName : OrderedLoadCommandNames)
	{
		const TSharedPtr<FToolPalette>* PalettePtr = LoadCommandNameToToolPaletteMap.Find(CommandName.ToString());
		if (!PalettePtr || !PalettePtr->IsValid())
		{
			continue;
		}

		const TSharedPtr<FToolPalette> Palette = *PalettePtr;
		const TSharedPtr<const FUICommandInfo> Cmd = Palette->LoadToolPaletteAction;
		if (!Cmd.IsValid())
		{
			continue;
		}

		const FName CmdName = Cmd->GetCommandName();
		const TSharedRef<const FUICommandInfo> CmdRef = Cmd.ToSharedRef();

		Column->AddSlot()
			.AutoHeight()
			.Padding(0.f, 1.f)
			[
				SNew(SCheckBox)
				.Style(&ToolBarStyle.ToggleButton)
				.ToolTipText(Cmd->GetDescription())
				.IsChecked_Lambda([this, CmdName]
					{
						return IsActiveToolPalette(CmdName);
					})
				.OnCheckStateChanged_Lambda([this, CmdRef](ECheckBoxState)
					{
						if (LoadToolPaletteCommandList.IsValid())
						{
							LoadToolPaletteCommandList->ExecuteAction(CmdRef);
						}
					})
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(ToolBarStyle.IconPaddingWithVisibleLabel)
					[
						SNew(SImage)
						.Image(Cmd->GetIcon().GetIcon())
						.DesiredSizeOverride(FVector2D(ToolBarStyle.IconSize))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(ToolBarStyle.LabelPadding)
					[
						SNew(STextBlock)
						.Text(Cmd->GetLabel())
						.TextStyle(&ToolBarStyle.LabelStyle)
						.Justification(ETextJustify::Center)
						.AutoWrapText(true)
						.WrapTextAt(CategoryLabelWrapAt)
					]
				]
			];
	}

	return SNew(SBox)
		.WidthOverride(CategoryColumnWidth)
		[
			Column
		];
}

TSharedPtr<SWidget> FMetaHumanCharacterEditorToolkitBuilder::GenerateWidget()
{
	if (!LoadPaletteToolBarBuilder.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	if (!MainContentVerticalBox.IsValid())
	{
		MainContentVerticalBox = SNew(SVerticalBox);
	}
	MainContentVerticalBox->ClearChildren();

	// Replicates FToolkitBuilder::UpdateContentForCategory because that method is
	// declared private in FToolkitBuilder and is therefore unreachable from this
	// derived class. Keep this in sync with ToolkitBuilder.cpp if the engine version changes.
	{
		TSharedPtr<SHorizontalBox> ToolNameHeaderBox;

		if (ToolkitSections.IsValid())
		{
			if (ToolkitSections->ModeWarningArea)
			{
				MainContentVerticalBox->AddSlot()
					.AutoHeight()
					.HAlign(HAlign_Fill)
					.Padding(5)
					[
						ToolkitSections->ModeWarningArea->AsShared()
					];
			}

			if (ToolkitSections->Header)
			{
				MainContentVerticalBox->AddSlot()
					.AutoHeight()
					.HAlign(HAlign_Fill)
					.Padding(0)
					[
						ToolkitSections->Header->AsShared()
					];
			}
		}

		MainContentVerticalBox->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			.Padding(0)
			[
				GetToolPaletteWidget()
			];

		MainContentVerticalBox->AddSlot()
			.AutoHeight()
			[
				*FSeparatorTemplates::SmallHorizontalBackgroundNoBorder().BindVisibility(
					TAttribute<EVisibility>::CreateLambda([this]()
					{
						return GetActivePaletteCommandsVisibility();
					}))
			];

		MainContentVerticalBox->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			.Padding(0)
			[
				SNew(SBorder)
				.HAlign(HAlign_Fill)
				.Padding(Style.ActiveToolTitleBorderPadding)
				.BorderImage(&Style.ToolDetailsBackgroundBrush)
				[
					SNew(SBorder)
					.Visibility_Lambda([this]()
					{
						return GetActiveToolDisplayName().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
					})
					.BorderImage(&Style.TitleBackgroundBrush)
					.Padding(Style.ToolContextTextBlockPadding)
					[
						SAssignNew(ToolNameHeaderBox, SHorizontalBox)
						+ SHorizontalBox::Slot()
						.Padding(0)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Left)
						[
							SNew(STextBlock)
							.Justification(ETextJustify::Left)
							.Margin(0)
							.Font(Style.TitleFont)
							.Text_Lambda([this]() { return GetActiveToolDisplayName(); })
							.ColorAndOpacity(Style.TitleForegroundColor)
						]
					]
				]
			];

		if (ToolkitSections.IsValid())
		{
			if (ToolkitSections->ToolPresetArea && ToolNameHeaderBox.IsValid())
			{
				ToolNameHeaderBox->AddSlot()
					.HAlign(HAlign_Right)
					[
						ToolkitSections->ToolPresetArea->AsShared()
					];
			}

			if (ToolkitSections->ToolWarningArea)
			{
				MainContentVerticalBox->AddSlot()
					.AutoHeight()
					.HAlign(HAlign_Fill)
					.Padding(5)
					[
						ToolkitSections->ToolWarningArea->AsShared()
					];
			}

			if (ToolkitSections->ToolHeader)
			{
				MainContentVerticalBox->AddSlot()
					.AutoHeight()
					.HAlign(HAlign_Fill)
					.Padding(0)
					[
						ToolkitSections->ToolHeader->AsShared()
					];
			}

			if (ToolkitSections->DetailsView)
			{
				MainContentVerticalBox->AddSlot()
					.HAlign(HAlign_Fill)
					.FillHeight(1.f)
					[
						SNew(SBorder)
						.BorderImage(&Style.ToolDetailsBackgroundBrush)
						.Padding(0.f, 2.f, 0.f, 2.f)
						[
							ToolkitSections->DetailsView->AsShared()
						]
					];
			}

			if (ToolkitSections->Footer)
			{
				MainContentVerticalBox->AddSlot()
					.AutoHeight()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Bottom)
					.Padding(0)
					[
						ToolkitSections->Footer->AsShared()
					];
			}
		}
	}

	if (ToolViewArea.IsValid())
	{
		if (ToolCustomWarningsArea.IsValid())
		{
			MainContentVerticalBox->AddSlot()
				.AutoHeight()
				[
					ToolCustomWarningsArea.ToSharedRef()
				];
		}

		MainContentVerticalBox->AddSlot()
			[
				ToolViewArea.ToSharedRef()
			];
	}

	if (!CachedRootWidget.IsValid())
	{
		CachedRootWidget = SNew(SSplitter)
			.PhysicalSplitterHandleSize(2.0f)
			+ SSplitter::Slot()
			.Resizable(false)
			.SizeRule(SSplitter::SizeToContent)
			[
				BuildCategoryColumnWidget()
			]
			+ SSplitter::Slot()
			.SizeRule(SSplitter::FractionOfParent)
			[
				MainContentVerticalBox.ToSharedRef()
			];
	}

	return CachedRootWidget;
}

#undef LOCTEXT_NAMESPACE
