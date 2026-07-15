// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPackageDestinationDialog.h"

#include "UI/MetaHumanStyleSet.h"

#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PackageDestinationDialog"

namespace UE::MetaHuman
{

void SPackageDestinationDialog::Construct(const FArguments& InArgs)
{
	OnModeSelectedCallback = InArgs._OnModeSelected;

	const int32 AssetCount = InArgs._AssetCount;
	// This dialog is only valid for multi-asset packaging. For single assets the caller
	// skips the dialog entirely and goes straight to SaveFileDialog.
	check(AssetCount > 1);

	const FText DescriptionText = FText::Format(
		LOCTEXT("DialogDescription", "How would you like to package these {0} assets?"),
		AssetCount);

	ChildSlot
	[
		SNew(SBorder)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(FMetaHumanStyleSet::Get().GetFloat("Instructions.PagePadding"))
		.BorderImage(FMetaHumanStyleSet::Get().GetBrush("Instructions.Background"))
		[
			SNew(SBox)
			.MinDesiredWidth(420.f)
			[
				SNew(SVerticalBox)

				// Description
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.f, 0.f, 0.f, 12.f))
				[
					SNew(STextBlock)
					.Text(DescriptionText)
					.AutoWrapText(true)
					.Font(FMetaHumanStyleSet::Get().GetFontStyle("Instructions.DefaultFont"))
				]

				// ── Option 1: Combined archive ────────────────────────────────
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.f, 0.f, 0.f, 8.f))
				[
					SNew(SBorder)
					.BorderImage(FMetaHumanStyleSet::Get().GetBrush("Instructions.ItemBackground"))
					.Padding(FMetaHumanStyleSet::Get().GetFloat("Instructions.ItemPadding"))
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("CombinedTitle", "Package as one archive"))
							.Font(FMetaHumanStyleSet::Get().GetFontStyle("Instructions.InstructionFont"))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.f, 6.f, 0.f, 0.f))
						[
							SNew(STextBlock)
							.Text(LOCTEXT("CombinedDesc",
								"Combines all selected assets into a single .mhpkg file. "
								"You will choose where to save it."))
							.AutoWrapText(true)
							.Font(FMetaHumanStyleSet::Get().GetFontStyle("Instructions.DefaultFont"))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Right)
						.Padding(FMargin(0.f, 10.f, 0.f, 0.f))
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
							.Text(LOCTEXT("CombinedButton", "Save to one file..."))
							.OnClicked(this, &SPackageDestinationDialog::OnCombinedClicked)
						]
					]
				]

				// ── Option 2: Separate archives ───────────────────────────────
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.f, 0.f, 0.f, 8.f))
				[
					SNew(SBorder)
					.BorderImage(FMetaHumanStyleSet::Get().GetBrush("Instructions.ItemBackground"))
					.Padding(FMetaHumanStyleSet::Get().GetFloat("Instructions.ItemPadding"))
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SeparateTitle", "Package as separate archives"))
							.Font(FMetaHumanStyleSet::Get().GetFontStyle("Instructions.InstructionFont"))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.f, 6.f, 0.f, 0.f))
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SeparateDesc",
								"Saves each asset as its own .mhpkg file named after the asset. "
								"You will choose a folder to save them into."))
							.AutoWrapText(true)
							.Font(FMetaHumanStyleSet::Get().GetFontStyle("Instructions.DefaultFont"))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Right)
						.Padding(FMargin(0.f, 10.f, 0.f, 0.f))
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.Text(LOCTEXT("SeparateButton", "Save to a folder..."))
							.OnClicked(this, &SPackageDestinationDialog::OnSeparateClicked)
						]
					]
				]

				// ── Cancel ────────────────────────────────────────────────────
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				.Padding(FMargin(0.f, 4.f, 0.f, 0.f))
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.Text(LOCTEXT("CancelButton", "Cancel"))
					.OnClicked(this, &SPackageDestinationDialog::OnCancelClicked)
				]
			]
		]
	];
}

FReply SPackageDestinationDialog::OnCombinedClicked()
{
	OnModeSelectedCallback.ExecuteIfBound(EPackageDestinationMode::CombinedArchive);
	CloseParentWindow();
	return FReply::Handled();
}

FReply SPackageDestinationDialog::OnSeparateClicked()
{
	OnModeSelectedCallback.ExecuteIfBound(EPackageDestinationMode::SeparateArchives);
	CloseParentWindow();
	return FReply::Handled();
}

FReply SPackageDestinationDialog::OnCancelClicked()
{
	OnModeSelectedCallback.ExecuteIfBound(EPackageDestinationMode::Cancelled);
	CloseParentWindow();
	return FReply::Handled();
}

void SPackageDestinationDialog::CloseParentWindow()
{
	TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (ParentWindow.IsValid())
	{
		ParentWindow->RequestDestroyWindow();
	}
}

} // namespace UE::MetaHuman

#undef LOCTEXT_NAMESPACE
