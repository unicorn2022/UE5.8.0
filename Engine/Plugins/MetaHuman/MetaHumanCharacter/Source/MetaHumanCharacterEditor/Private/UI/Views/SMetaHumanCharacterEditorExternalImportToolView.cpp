// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetaHumanCharacterEditorExternalImportToolView.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"

#define LOCTEXT_NAMESPACE "SMetaHumanCharacterEditorExternalImportToolView"

void SMetaHumanCharacterEditorExternalImportToolView::Construct(
	const FArguments&, TSharedRef<SWidget> InContent, UMetaHumanCharacterExternalImportTool* InExtTool)
{
	// Store both before calling base Construct — it calls MakeToolView() synchronously.
	ExtTool = InExtTool;
	ExternalContent = InContent;

	SMetaHumanCharacterEditorToolView::Construct(SMetaHumanCharacterEditorToolView::FArguments(), InExtTool);
}

UInteractiveToolPropertySet* SMetaHumanCharacterEditorExternalImportToolView::GetToolProperties() const
{
	// External tools manage their own properties; the host view has no property set to expose.
	return nullptr;
}

void SMetaHumanCharacterEditorExternalImportToolView::MakeToolView()
{
	if (ToolViewScrollBox.IsValid())
	{
		ToolViewScrollBox->AddSlot()
			.VAlign(VAlign_Top)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateImportToolViewWarningSection()
				]

				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					ExternalContent.ToSharedRef()
				]
			];
	}

	if (ToolViewMainBox.IsValid())
	{
		ToolViewMainBox->AddSlot()
			.Padding(0.f, 4.f, 0.f, 0.f)
			.AutoHeight()
			[
				CreateImportToolViewButtonSection()
			];
	}
}

bool SMetaHumanCharacterEditorExternalImportToolView::IsImportButtonEnabled() const
{
	return ExtTool.IsValid() && ExtTool->CanApply();
}

FReply SMetaHumanCharacterEditorExternalImportToolView::OnImportButtonClicked()
{
	if (ExtTool.IsValid())
	{
		ExtTool->Apply();
	}
	return FReply::Handled();
}

FText SMetaHumanCharacterEditorExternalImportToolView::GetWarningText() const
{
	return ExtTool.IsValid() ? ExtTool->GetWarningText() : FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
