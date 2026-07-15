// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetaHumanCharacterEditorExportToolView.h"

#include "IDetailsView.h"
#include "MetaHumanCharacterEditorStyle.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Tools/MetaHumanCharacterEditorExportToolBase.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "SWarningOrErrorBox.h"

#define LOCTEXT_NAMESPACE "SMetaHumanCharacterEditorExportToolView"

void SMetaHumanCharacterEditorExportToolView::Construct(const FArguments& InArgs, UMetaHumanCharacterEditorExportToolBase* InTool)
{
	SMetaHumanCharacterEditorToolView::Construct(SMetaHumanCharacterEditorToolView::FArguments(), InTool);
}

UInteractiveToolPropertySet* SMetaHumanCharacterEditorExportToolView::GetToolProperties() const
{
	const UMetaHumanCharacterEditorExportToolBase* ExportTool = Cast<UMetaHumanCharacterEditorExportToolBase>(Tool);
	return IsValid(ExportTool) ? ExportTool->GetExportProperties() : nullptr;
}

void SMetaHumanCharacterEditorExportToolView::MakeToolView()
{
	if (ToolViewScrollBox.IsValid())
	{
		ToolViewScrollBox->AddSlot()
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.Padding(4.f, 10.f)
				.AutoHeight()
				[
					CreateExportToolViewDetailsViewSection()
				]
			];
	}

	if (ToolViewMainBox.IsValid())
	{
		ToolViewMainBox->AddSlot()
			.Padding(0.f, 4.f, 0.f, 0.f)
			.AutoHeight()
			[
				CreateExportToolViewExportSection()
			];
	}
}

void SMetaHumanCharacterEditorExportToolView::NotifyPreChange(FProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange)
	{
		OnPreEditChangeProperty(PropertyAboutToChange, PropertyAboutToChange->GetName());
	}
}

void SMetaHumanCharacterEditorExportToolView::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	if (PropertyThatChanged)
	{
		const bool bIsInteractive = PropertyChangedEvent.ChangeType & EPropertyChangeType::Interactive;
		OnPostEditChangeProperty(PropertyThatChanged, bIsInteractive);
	}
}

TSharedRef<SWidget> SMetaHumanCharacterEditorExportToolView::CreateExportToolViewDetailsViewSection()
{
	UInteractiveToolPropertySet* Properties = GetToolProperties();
	if (!Properties)
	{
		return SNullWidget::NullWidget;
	}

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(FName("PropertyEditor"));
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bUpdatesFromSelection = false;
	DetailsViewArgs.bLockable = false;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.NotifyHook = this;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObject(Properties);
	return DetailsView.ToSharedRef();
}

TSharedRef<SWidget> SMetaHumanCharacterEditorExportToolView::CreateExportToolViewExportSection()
{
	const UMetaHumanCharacterEditorExportToolBase* ExportTool = Cast<UMetaHumanCharacterEditorExportToolBase>(Tool);

	return
		SNew(SBorder)
		.Padding(-4.f)
		.BorderImage(FMetaHumanCharacterEditorStyle::Get().GetBrush("MetaHumanCharacterEditorTools.ActiveToolLabel"))
		[
			SNew(SVerticalBox)

			// Warning label
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.Padding(4.f)
				[
					SNew(SWarningOrErrorBox)
					.AutoWrapText(true)
					.MessageStyle(EMessageStyle::Warning)
					.Visibility(this, &SMetaHumanCharacterEditorExportToolView::GetWarningVisibility)
					.Message_Lambda([this]() { return ExportErrorMsg; })
				]
			]

			// Export button
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(50.f)
				.HAlign(HAlign_Fill)
				.Padding(10.f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), FName("FlatButton.Success"))
					.ForegroundColor(FLinearColor::White)
					.IsEnabled(this, &SMetaHumanCharacterEditorExportToolView::IsExportButtonEnabled)
					.OnClicked(this, &SMetaHumanCharacterEditorExportToolView::OnExportButtonClicked)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(IsValid(ExportTool) ? ExportTool->GetExportButtonText() : LOCTEXT("FallbackExportButtonText", "Export"))
						.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					]
				]
			]
		];
}

bool SMetaHumanCharacterEditorExportToolView::IsExportButtonEnabled() const
{
	if (const UMetaHumanCharacterEditorExportToolBase* ExportTool = Cast<UMetaHumanCharacterEditorExportToolBase>(Tool))
	{
		FText ErrorMsg;
		return ExportTool->CanExport(ErrorMsg);
	}

	return false;
}

FReply SMetaHumanCharacterEditorExportToolView::OnExportButtonClicked() const
{
	if (const UMetaHumanCharacterEditorExportToolBase* ExportTool = Cast<UMetaHumanCharacterEditorExportToolBase>(Tool))
	{
		ExportTool->Export();
	}

	return FReply::Handled();
}

EVisibility SMetaHumanCharacterEditorExportToolView::GetWarningVisibility() const
{
	if (const UMetaHumanCharacterEditorExportToolBase* ExportTool = Cast<UMetaHumanCharacterEditorExportToolBase>(Tool))
	{
		if (ExportTool->CanExport(ExportErrorMsg))
		{
			return EVisibility::Collapsed;
		}
	}

	return EVisibility::HitTestInvisible;
}

#undef LOCTEXT_NAMESPACE
