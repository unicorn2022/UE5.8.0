// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorExportToolBase.h"

#include "GameFramework/Actor.h"
#include "InteractiveToolManager.h"
#include "MetaHumanCharacterEditorActorInterface.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "ToolTargetManager.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditorExportToolBase"

// Builder

UInteractiveTool* UMetaHumanCharacterEditorExportToolBuilder::BuildTool(const FToolBuilderState& InSceneState) const
{
	UToolTarget* Target = InSceneState.TargetManager->BuildFirstSelectedTargetable(InSceneState, GetTargetRequirements());
	check(Target);
	check(ToolClass);

	UMetaHumanCharacterEditorExportToolBase* NewTool = NewObject<UMetaHumanCharacterEditorExportToolBase>(InSceneState.ToolManager, ToolClass);
	NewTool->SetTarget(Target);

	// Reuse the same property object across activations so details-panel edits are undoable.
	// Outer is the transient package so Modify() doesn't dirty any asset.
	if (UClass* PropertiesClass = NewTool->GetExportPropertiesClass())
	{
		if (!IsValid(PersistentProperties.Get()))
		{
			PersistentProperties.Reset(NewObject<UInteractiveToolPropertySet>(
				GetTransientPackage(),
				PropertiesClass,
				NAME_None,
				RF_Transactional));
		}

		NewTool->InitializeExportProperties(PersistentProperties.Get());
	}

	return NewTool;
}

// Base Tool

void UMetaHumanCharacterEditorExportToolBase::Setup()
{
	Super::Setup();

	SetToolDisplayName(GetExportToolDisplayName());

	// ExportProperties is expected to have been assigned by the builder. If it's missing the
	// tool can still run, but its UI will be empty and undo will not function for it.
	check(IsValid(ExportProperties));
	AddToolPropertySource(ExportProperties);
}

void UMetaHumanCharacterEditorExportToolBase::Shutdown(EToolShutdownType ShutdownType)
{
	if (IsValid(ExportProperties))
	{
		RemoveToolPropertySource(ExportProperties);
	}

	Super::Shutdown(ShutdownType);
}

bool UMetaHumanCharacterEditorExportToolBase::CanExport(FText& OutErrorMsg) const
{
	return true;
}

void UMetaHumanCharacterEditorExportToolBase::Export() const
{
}

FText UMetaHumanCharacterEditorExportToolBase::GetExportButtonText() const
{
	return LOCTEXT("DefaultExportButtonText", "Export");
}

#undef LOCTEXT_NAMESPACE
