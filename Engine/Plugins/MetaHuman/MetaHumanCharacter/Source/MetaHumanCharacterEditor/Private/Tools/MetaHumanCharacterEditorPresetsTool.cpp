// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorPresetsTool.h"

#include "MetaHumanCharacterEditorActor.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterEditorViewportClient.h"
#include "ObjectTools.h"
#include "Tools/MetaHumanCharacterEditorToolTargetUtil.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "ToolTargetManager.h"
#include "InteractiveToolManager.h"
#include "Editor/EditorEngine.h"
#include "ToolBuilderUtil.h"


extern UNREALED_API UEditorEngine* GEditor;

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditor"

class FPresetsToolCommandChange : public FToolCommandChange
{
public:

	FPresetsToolCommandChange(TNotNull<UMetaHumanCharacter*> InOldPresetCharacter,
		TNotNull<UMetaHumanCharacter*> InNewPresetCharacter,
		TNotNull<UInteractiveToolManager*> InToolManager)
		: OldPresetCharacter(InOldPresetCharacter)
		, NewPresetCharacter(InNewPresetCharacter)
		, ToolManager{ InToolManager }
	{
	}

	//~Begin FToolCommandChange interface
	virtual FString ToString() const override
	{
		return TEXT("MetaHuman Character Presets");
	}

	virtual bool HasExpired(UObject* InObject) const override
	{
		return !ToolManager.IsValid() || !OldPresetCharacter.IsValid() || !NewPresetCharacter.IsValid();
	}

	void Apply(UObject* InObject) override
	{
		if (NewPresetCharacter.IsValid())
		{
			ApplyPresetCharacter(InObject, NewPresetCharacter.Get());
		}
	}

	void Revert(UObject* InObject) override
	{
		if (OldPresetCharacter.IsValid())
		{
			ApplyPresetCharacter(InObject, OldPresetCharacter.Get());
		}
	}
	//~End FToolCommandChange interface

protected:
	void ApplyPresetCharacter(UObject* InObject, const TNotNull<UMetaHumanCharacter*> InPresetCharacter) const
	{
		UMetaHumanCharacter* Character = CastChecked<UMetaHumanCharacter>(InObject);

		Character->Modify();

		UMetaHumanCharacterEditorSubsystem::Get()->InitializeFromPreset(Character, InPresetCharacter);

		FViewport* Viewport = ToolManager->GetContextQueriesAPI()->GetFocusedViewport();
		FMetaHumanCharacterViewportClient* MetaHumanCharacterViewportClient = static_cast<FMetaHumanCharacterViewportClient*>(Viewport->GetClient());
		if (MetaHumanCharacterViewportClient)
		{
			MetaHumanCharacterViewportClient->RescheduleFocus();
		}
	}

	// OldPresetCharacter is strong because in our use-case of FPresetsToolCommandChange its passed as transient, and we don't want it to be GC'd
	TStrongObjectPtr<UMetaHumanCharacter> OldPresetCharacter;
	TWeakObjectPtr<UMetaHumanCharacter> NewPresetCharacter;
	TWeakObjectPtr<UInteractiveToolManager> ToolManager;
};

UInteractiveTool* UMetaHumanCharacterEditorPresetsToolBuilder::BuildTool(const FToolBuilderState& InSceneState) const
{
	UToolTarget* Target = InSceneState.TargetManager->BuildFirstSelectedTargetable(InSceneState, GetTargetRequirements());
	check(Target);

	UMetaHumanCharacterEditorPresetsTool* PresetsTool = NewObject<UMetaHumanCharacterEditorPresetsTool>(InSceneState.ToolManager);
	PresetsTool->SetTarget(Target);

	return PresetsTool;
}

bool UMetaHumanCharacterEditorPresetsToolBuilder::CanBuildTool(const FToolBuilderState& InSceneState) const
{
	return Super::CanBuildTool(InSceneState)
		&& !IsCharacterRequestingHighResTextures(InSceneState)
		&& !IsCharacterRigged(InSceneState);
}

bool UMetaHumanCharacterEditorPresetsToolProperties::CanEditChange(const FProperty* InProperty) const
{
	bool bIsEditable = Super::CanEditChange(InProperty);

	if (bIsEditable && InProperty != nullptr)
	{
		UMetaHumanCharacterEditorPresetsTool* PresetsTool = GetTypedOuter<UMetaHumanCharacterEditorPresetsTool>();
		check(PresetsTool);

		UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(PresetsTool->GetTarget());
		check(Character);
	}

	return bIsEditable;
}

void UMetaHumanCharacterEditorPresetsTool::Setup()
{
	Super::Setup();

	SetToolDisplayName(LOCTEXT("PresetsToolName", "Presets"));

	PresetsProperties = NewObject<UMetaHumanCharacterEditorPresetsToolProperties>(this);
	AddToolPropertySource(PresetsProperties);

	UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	check(Character);
}

void UMetaHumanCharacterEditorPresetsTool::OnPropertyModified(UObject* InPropertySet, FProperty* InProperty)
{
	if (InPropertySet != PresetsProperties || !InProperty)
	{
		return;
	}

	if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterPresetsLibraryProperties, ProjectPath))
	{
		ObjectTools::SanitizeInvalidCharsInline(PresetsProperties->LibraryManagement.ProjectPath.Path, INVALID_LONGPACKAGE_CHARACTERS);
	}
	else if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterPresetsLibraryProperties, Path))
	{
		ObjectTools::SanitizeInvalidCharsInline(PresetsProperties->LibraryManagement.Path.Path, INVALID_LONGPACKAGE_CHARACTERS);
	}
	else if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterPresetsManagementProperties, ImagePath))
	{
		ObjectTools::SanitizeInvalidCharsInline(PresetsProperties->PresetsManagement.ImagePath.Path, INVALID_LONGPACKAGE_CHARACTERS);
	}
}

void UMetaHumanCharacterEditorPresetsTool::ApplyPresetCharacter(TNotNull<UMetaHumanCharacter*> InPresetCharacter)
{
	TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);

	// Duplicating the character as it will be used as OldPresetCharacter for undo
	UMetaHumanCharacter* OldPresetCharacter = DuplicateObject<UMetaHumanCharacter>(static_cast<UMetaHumanCharacter*>(Character), nullptr);
	TUniquePtr<FPresetsToolCommandChange> CommandChange = MakeUnique<FPresetsToolCommandChange>(OldPresetCharacter, InPresetCharacter, GetToolManager());
	GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("PresetsToolCommandChangeTransaction", "Apply Preset Character"));

	UMetaHumanCharacterEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMetaHumanCharacterEditorSubsystem>();
	Subsystem->InitializeFromPreset(Character, InPresetCharacter);

	FViewport* Viewport = GetToolManager()->GetContextQueriesAPI()->GetFocusedViewport();
	FMetaHumanCharacterViewportClient* MetaHumanCharacterViewportClient = static_cast<FMetaHumanCharacterViewportClient*>(Viewport->GetClient());
	if (MetaHumanCharacterViewportClient)
	{
		MetaHumanCharacterViewportClient->RescheduleFocus();
	}
}

#undef LOCTEXT_NAMESPACE
