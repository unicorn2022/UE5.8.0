// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetaHumanCharacterEditorTextureMaterialOverrideToolView.h"

#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "Tools/MetaHumanCharacterEditorTextureMaterialOverrideTool.h"
#include "Widgets/Layout/SScrollBox.h"

#define LOCTEXT_NAMESPACE "SMetaHumanCharacterEditorTextureMaterialOverrideToolView"

void SMetaHumanCharacterEditorTextureMaterialOverrideToolView::Construct(const FArguments& InArgs, UMetaHumanCharacterEditorTextureMaterialOverrideTool* InTool)
{
	SMetaHumanCharacterEditorToolView::Construct(SMetaHumanCharacterEditorToolView::FArguments(), InTool);
}

UInteractiveToolPropertySet* SMetaHumanCharacterEditorTextureMaterialOverrideToolView::GetToolProperties() const
{
	const UMetaHumanCharacterEditorTextureMaterialOverrideTool* OverrideTool = Cast<UMetaHumanCharacterEditorTextureMaterialOverrideTool>(Tool);
	return IsValid(OverrideTool) ? OverrideTool->GetTextureMaterialOverrideToolProperties() : nullptr;
}

void SMetaHumanCharacterEditorTextureMaterialOverrideToolView::MakeToolView()
{
	UMetaHumanCharacterEditorTextureMaterialOverrideToolProperties* OverrideToolProperties = Cast<UMetaHumanCharacterEditorTextureMaterialOverrideToolProperties>(GetToolProperties());
	if (!OverrideToolProperties || !ToolViewScrollBox.IsValid())
	{
		return;
	}

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bUpdatesFromSelection = false;
	DetailsViewArgs.bLockable = false;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.NotifyHook = this;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	TSharedRef<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObject(OverrideToolProperties);

	ToolViewScrollBox->AddSlot()
		.VAlign(VAlign_Top)
		[
			DetailsView
		];
}

void SMetaHumanCharacterEditorTextureMaterialOverrideToolView::NotifyPreChange(FProperty* PropertyAboutToChange)
{
	OnPreEditChangeProperty(PropertyAboutToChange, PropertyAboutToChange->GetName());
}

void SMetaHumanCharacterEditorTextureMaterialOverrideToolView::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	const bool bIsInteractive = PropertyChangedEvent.ChangeType & EPropertyChangeType::Interactive;
	OnPostEditChangeProperty(PropertyThatChanged, bIsInteractive);
}

#undef LOCTEXT_NAMESPACE
