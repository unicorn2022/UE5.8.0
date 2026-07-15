// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorTextureMaterialOverrideTool.h"
#include "MetaHumanCharacterEditorActor.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "Tools/MetaHumanCharacterEditorToolTargetUtil.h"
#include "ToolBuilderUtil.h"
#include "ToolTargetManager.h"
#include "InteractiveToolManager.h"
#include "InteractiveToolChange.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditorTextureMaterialOverrideTool"

// Undo command for keeping track of changes in texture override settings
class FMetaHumanCharacterEditorTextureMaterialOverrideToolCommandChange : public FToolCommandChange
{
public:
	FMetaHumanCharacterEditorTextureMaterialOverrideToolCommandChange(
		const FMetaHumanCharacterSkinSettings& InOldSkinSettings,
		const FMetaHumanCharacterSkinSettings& InNewSkinSettings,
		TNotNull<UInteractiveToolManager*> InToolManager)
		: OldSkinSettings(InOldSkinSettings)
		, NewSkinSettings(InNewSkinSettings)
		, ToolManager(InToolManager)
	{
	}

	//~Begin FToolCommandChange interface
	virtual FString ToString() const override
	{
		return TEXT("MetaHuman Character Edit Texture & Material Overrides");
	}

	virtual bool HasExpired(UObject* InObject) const override
	{
		return !ToolManager.IsValid();
	}

	virtual void Apply(UObject* InObject) override
	{
		UMetaHumanCharacter* MetaHumanCharacter = CastChecked<UMetaHumanCharacter>(InObject);
		UMetaHumanCharacterEditorSubsystem::Get()->CommitSkinSettings(MetaHumanCharacter, NewSkinSettings);

		UpdateToolProperties(NewSkinSettings);
	}

	virtual void Revert(UObject* InObject) override
	{
		UMetaHumanCharacter* MetaHumanCharacter = CastChecked<UMetaHumanCharacter>(InObject);
		UMetaHumanCharacterEditorSubsystem::Get()->CommitSkinSettings(MetaHumanCharacter, OldSkinSettings);

		UpdateToolProperties(OldSkinSettings);
	}
	//~End FToolCommandChange interface

private:

	void UpdateToolProperties(const FMetaHumanCharacterSkinSettings& InSkinSettings) const
	{
		if (ToolManager.IsValid())
		{
			if (UMetaHumanCharacterEditorTextureMaterialOverrideTool* Tool = Cast<UMetaHumanCharacterEditorTextureMaterialOverrideTool>(ToolManager->GetActiveTool(EToolSide::Left)))
			{
				UMetaHumanCharacterEditorTextureMaterialOverrideToolProperties* Properties = nullptr;
				if (Tool->GetToolProperties().FindItemByClass<UMetaHumanCharacterEditorTextureMaterialOverrideToolProperties>(&Properties))
				{
					Properties->CopyFrom(InSkinSettings);
					Properties->SilentUpdateWatched();

					Tool->PreviousSkinSettings = InSkinSettings;
				}
			}
		}
	}

	FMetaHumanCharacterSkinSettings OldSkinSettings;
	FMetaHumanCharacterSkinSettings NewSkinSettings;
	TWeakObjectPtr<UInteractiveToolManager> ToolManager;
};

UInteractiveTool* UMetaHumanCharacterEditorTextureMaterialOverrideToolBuilder::BuildTool(const FToolBuilderState& InSceneState) const
{
	UToolTarget* Target = InSceneState.TargetManager->BuildFirstSelectedTargetable(InSceneState, GetTargetRequirements());
	check(Target);

	UMetaHumanCharacterEditorTextureMaterialOverrideTool* Tool = NewObject<UMetaHumanCharacterEditorTextureMaterialOverrideTool>(InSceneState.ToolManager);
	Tool->SetTarget(Target);

	return Tool;
}

void UMetaHumanCharacterEditorTextureMaterialOverrideToolProperties::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	OnPropertyValueSetDelegate.ExecuteIfBound(PropertyChangedEvent);
}

void UMetaHumanCharacterEditorTextureMaterialOverrideToolProperties::CopyTo(FMetaHumanCharacterSkinSettings& OutSkinSettings) const
{
	OutSkinSettings.TextureMaterialOverrides = TextureMaterialOverrides;
}

void UMetaHumanCharacterEditorTextureMaterialOverrideToolProperties::CopyFrom(const FMetaHumanCharacterSkinSettings& InSkinSettings)
{
	TextureMaterialOverrides = InSkinSettings.TextureMaterialOverrides;
}

void UMetaHumanCharacterEditorTextureMaterialOverrideTool::Setup()
{
	Super::Setup();

	SetToolDisplayName(LOCTEXT("TextureMaterialOverrideToolName", "Texture & Material Overrides"));

	ToolProperties = NewObject<UMetaHumanCharacterEditorTextureMaterialOverrideToolProperties>(this);
	AddToolPropertySource(ToolProperties);

	const UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	check(Character);

	PreviousSkinSettings = Character->SkinSettings;
	ToolProperties->CopyFrom(Character->SkinSettings);

	// Enable show teeth expression by default when opening the tool
	UpdateShowTeethState();

	// Bind to the ValueSet event to fill in the undo stack
	ToolProperties->OnPropertyValueSetDelegate.BindWeakLambda(this, [this](const FPropertyChangedEvent& PropertyChangedEvent)
	{
		if (UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target))
		{
			if ((PropertyChangedEvent.ChangeType & (EPropertyChangeType::ValueSet | EPropertyChangeType::ResetToDefault)) != 0u)
			{
				if (PropertyChangedEvent.Property->GetName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorTextureMaterialOverrideToolProperties, bShowTeeth))
				{
					UpdateShowTeethState();
				}
				else
				{
					// Build merged settings: start from character's current settings, overlay our overrides
					FMetaHumanCharacterSkinSettings NewSkinSettings = Character->SkinSettings;
					ToolProperties->CopyTo(NewSkinSettings);

					TUniquePtr<FMetaHumanCharacterEditorTextureMaterialOverrideToolCommandChange> CommandChange =
						MakeUnique<FMetaHumanCharacterEditorTextureMaterialOverrideToolCommandChange>(PreviousSkinSettings, NewSkinSettings, GetToolManager());
					GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("TextureMaterialOverrideCommandChange", "Edit Texture & Material Overrides"));

					PreviousSkinSettings = NewSkinSettings;

					UpdateTextureMaterialOverrideState();
				}
			}
		}
	});

	ToolProperties->SilentUpdateWatched();
}

void UMetaHumanCharacterEditorTextureMaterialOverrideTool::Shutdown(EToolShutdownType InShutdownType)
{
	UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	check(Character);

	// Reset show teeth expression on tool shutdown
	Character->HeadModelSettings.Teeth.EnableShowTeethExpression = false;
	UMetaHumanCharacterEditorSubsystem::Get()->ApplyHeadModelSettings(Character, Character->HeadModelSettings);

	// Commit final state
	FMetaHumanCharacterSkinSettings CurrentSkinSettings = Character->SkinSettings;
	ToolProperties->CopyTo(CurrentSkinSettings);
	UMetaHumanCharacterEditorSubsystem::Get()->CommitSkinSettings(Character, CurrentSkinSettings);
}

void UMetaHumanCharacterEditorTextureMaterialOverrideTool::UpdateTextureMaterialOverrideState() const
{
	UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	check(Character);

	FMetaHumanCharacterSkinSettings MergedSettings = Character->SkinSettings;
	ToolProperties->CopyTo(MergedSettings);

	UMetaHumanCharacterEditorSubsystem::Get()->ApplySkinSettings(Character, MergedSettings);
}

void UMetaHumanCharacterEditorTextureMaterialOverrideTool::UpdateShowTeethState() const
{
	UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	check(Character);

	Character->HeadModelSettings.Teeth.EnableShowTeethExpression = ToolProperties->bShowTeeth;
	UMetaHumanCharacterEditorSubsystem::Get()->ApplyHeadModelSettings(Character, Character->HeadModelSettings);
}

#undef LOCTEXT_NAMESPACE
