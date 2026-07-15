// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorWardrobeTools.h"

#include "Editor/EditorEngine.h"
#include "InteractiveToolManager.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanInstance.h"
#include "MetaHumanCharacterEditorActorInterface.h"
#include "MetaHumanCharacterEditorSettings.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCollection.h"
#include "SceneManagement.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "Tools/MetaHumanCharacterEditorToolTargetUtil.h"
#include "ToolTargetManager.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditor"

UInteractiveTool* UMetaHumanCharacterEditorWardrobeToolBuilder::BuildTool(const FToolBuilderState& InSceneState) const
{
	UToolTarget* Target = InSceneState.TargetManager->BuildFirstSelectedTargetable(InSceneState, GetTargetRequirements());
	check(Target);

	switch (ToolType)
	{
		case EMetaHumanCharacterWardrobeEditingTool::Wardrobe:
		{
			UMetaHumanCharacterEditorWardrobeTool* WardrobeTool = NewObject<UMetaHumanCharacterEditorWardrobeTool>(InSceneState.ToolManager);
			WardrobeTool->SetTarget(Target);
			WardrobeTool->SetTargetWorld(InSceneState.World);
			return WardrobeTool;
		}

		default:
			checkNoEntry();
	}

	return nullptr;
}

void UMetaHumanCharacterEditorWardrobeTool::Setup()
{
	Super::Setup();

	SetToolDisplayName(LOCTEXT("WardrobeToolName", "Wardrobe"));

	UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	check(Character);

	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();

	PropertyObject = NewObject<UMetaHumanCharacterEditorWardrobeToolProperties>(this);
	PropertyObject->Collection = Subsystem->GetPreviewCollection(Character);
	PropertyObject->Character = Character;

	AddToolPropertySource(PropertyObject);

	// Rebuild tool when the character's wardrobe paths change
	WardrobePathChangedCharacter = Character->OnWardrobePathsChanged.AddUObject(
		this,
		&UMetaHumanCharacterEditorWardrobeTool::OnWardrobePathsChanged);

	// Rebuild tool when the user settings' wardrobe paths change
	WardrobePathChangedUserSettings = GetMutableDefault<UMetaHumanCharacterEditorSettings>()->OnWardrobePathsChanged.AddUObject(
		this,
		&UMetaHumanCharacterEditorWardrobeTool::OnWardrobePathsChanged);
}

void UMetaHumanCharacterEditorWardrobeTool::Shutdown(EToolShutdownType InShutdownType)
{
	UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	if (Character)
	{
		Character->OnWardrobePathsChanged.Remove(WardrobePathChangedCharacter);
		WardrobePathChangedCharacter.Reset();
	}

	GetMutableDefault<UMetaHumanCharacterEditorSettings>()->OnWardrobePathsChanged.Remove(WardrobePathChangedUserSettings);
	WardrobePathChangedUserSettings.Reset();
}

void UMetaHumanCharacterEditorWardrobeTool::OnWardrobePathsChanged()
{
	// Reactivate the same tool, the previous one will shut down
	GetToolManager()->ActivateTool(EToolSide::Left);
}

#undef LOCTEXT_NAMESPACE 
