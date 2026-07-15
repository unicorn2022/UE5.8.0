// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAFChooserTableAssetDefinition.h"
#include "IWorkspaceEditorModule.h"
#include "Workspace/AnimNextWorkspaceFactory.h"
#include "ChooserEditorStyle.h"
#include "Modules/ModuleManager.h"

EAssetCommandResult UAssetDefinition_UAFAnimChooserTable::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	using namespace UE::Workspace;

	for (UUAFAnimChooserTable* Asset : OpenArgs.LoadObjects<UUAFAnimChooserTable>())
	{
		IWorkspaceEditorModule& WorkspaceEditorModule = FModuleManager::Get().LoadModuleChecked<IWorkspaceEditorModule>("WorkspaceEditor");
		WorkspaceEditorModule.OpenWorkspaceForObject(Asset, EOpenWorkspaceMethod::Default, UAnimNextWorkspaceFactory::StaticClass());
	}

	return EAssetCommandResult::Handled;
}

const FSlateBrush* UAssetDefinition_UAFAnimChooserTable::GetThumbnailBrush(const FAssetData& InAssetData, const FName InClassName) const
{
	return UE::ChooserEditor::FChooserEditorStyle::Get().GetBrush("ChooserEditor.ChooserTableIconLarge");
}

const FSlateBrush* UAssetDefinition_UAFAnimChooserTable::GetIconBrush(const FAssetData& InAssetData, const FName InClassName) const
{
	return UE::ChooserEditor::FChooserEditorStyle::Get().GetBrush("ChooserEditor.ChooserTableIconSmall");
}