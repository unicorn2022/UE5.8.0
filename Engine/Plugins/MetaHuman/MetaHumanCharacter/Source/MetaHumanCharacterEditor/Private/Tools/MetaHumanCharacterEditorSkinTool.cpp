// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorSkinTool.h"
#include "MetaHumanCharacterEditorActor.h"
#include "MetaHumanCharacterEditorSettings.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanInvisibleDrivingActor.h"
#include "MetaHumanCharacterAnimInstance.h"
#include "MetaHumanCharacterEditorToolCommandChange.h"
#include "MetaHumanFaceTextureSynthesizer.h"
#include "Animation/AnimSequence.h"
#include "Containers/Ticker.h"
#include "Tools/MetaHumanCharacterEditorToolTargetUtil.h"
#include "ToolBuilderUtil.h"
#include "ToolTargetManager.h"
#include "InteractiveToolManager.h"
#include "InteractiveToolChange.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "Editor/EditorEngine.h"
#include "Misc/MessageDialog.h"
#include "UObject/Package.h"
#include "UObject/ObjectSaveContext.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditorSkinTool"

// Undo command for keeping track of changes in the Character skin settings
class FMetaHumanCharacterEditorSkinToolCommandChange : public FToolCommandChange
{
public:
	FMetaHumanCharacterEditorSkinToolCommandChange(const FMetaHumanCharacterSkinSettings& InOldSkinSettings,
												   const FMetaHumanCharacterSkinSettings& InNewSkinSettings,
												   TNotNull<UInteractiveToolManager*> InToolManager)
		: OldSkinSettings(InOldSkinSettings)
		, NewSkinSettings(InNewSkinSettings)
		, ToolManager{ InToolManager }
	{
	}

	//~Begin FToolCommandChange interface
	virtual FString ToString() const override
	{
		return TEXT("MetaHuman Character Edit Skin");
	}

	virtual bool HasExpired(UObject* InObject) const override
	{
		// If the ToolManager is not valid anymore it means the asset editor was closed so mark the transaction as expired
		return !ToolManager.IsValid();
	}

	void Apply(UObject* InObject) override
	{
		UMetaHumanCharacter* MetaHumanCharacter = CastChecked<UMetaHumanCharacter>(InObject);
		UMetaHumanCharacterEditorSubsystem::Get()->CommitSkinSettings(MetaHumanCharacter, NewSkinSettings);

		UpdateSkinToolProperties(NewSkinSettings);
	}

	void Revert(UObject* InObject) override
	{
		UMetaHumanCharacter* MetaHumanCharacter = CastChecked<UMetaHumanCharacter>(InObject);
		UMetaHumanCharacterEditorSubsystem::Get()->CommitSkinSettings(MetaHumanCharacter, OldSkinSettings);

		UpdateSkinToolProperties(OldSkinSettings);
	}
	//~End FToolCommandChange interface

protected:

	/**
	 * Updates the Skin Tool Properties of the active tool using the given skin settings
	 */
	void UpdateSkinToolProperties(const FMetaHumanCharacterSkinSettings& InSkinSettings)
	{
		if (ToolManager.IsValid())
		{
			if (UMetaHumanCharacterEditorSkinTool* SkinTool = Cast<UMetaHumanCharacterEditorSkinTool>(ToolManager->GetActiveTool(EToolSide::Left)))
			{
				UMetaHumanCharacterEditorSkinToolProperties* SkinToolProperties = nullptr;
				if (SkinTool->GetToolProperties().FindItemByClass<UMetaHumanCharacterEditorSkinToolProperties>(&SkinToolProperties))
				{
					SkinToolProperties->CopyFrom(InSkinSettings);
					SkinToolProperties->SilentUpdateWatched();

					// Restore the PreviousSkinSettings of the tool to what we are applying so that
					// new commands are created with the correct previous settings
					SkinTool->PreviousSkinSettings = InSkinSettings;

					SkinToolProperties->OnSkinToolCommandChangeApplied.Broadcast();
				}
			}
		}
	}

protected:

	// Store as FMetaHumanCharacterSkinSettings since it is simpler to manage the lifetime of structs
	FMetaHumanCharacterSkinSettings OldSkinSettings;
	FMetaHumanCharacterSkinSettings NewSkinSettings;

	// Reference to skin tool manager, used to update the skin tool properties when applying transactions
	TWeakObjectPtr<UInteractiveToolManager> ToolManager;
};

// Specialized version of the the skin edit command that also updates the face state HF variant
class FMetaHumanCharacterEditorSkinTextureCommandChange : public FMetaHumanCharacterEditorSkinToolCommandChange
{
public:
	FMetaHumanCharacterEditorSkinTextureCommandChange(const FMetaHumanCharacterSkinSettings& InOldSkinSettings,
													  const FMetaHumanCharacterSkinSettings& InNewSkinSettings,
													  TSharedRef<FMetaHumanCharacterIdentity::FState> InReferenceFaceState,
													  TNotNull<UInteractiveToolManager*> InToolManager)
		: FMetaHumanCharacterEditorSkinToolCommandChange(InOldSkinSettings, InNewSkinSettings, InToolManager)
		, ReferenceFaceState(InReferenceFaceState)
	{
	}

	//~Begin FToolCommandChange interface
	virtual FString ToString() const override
	{
		return TEXT("MetaHuman Character Edit Skin Texture");
	}

	void Apply(UObject* InObject) override
	{
		ApplySkinSettingsAndHFVariant(InObject, NewSkinSettings);

	}

	void Revert(UObject* InObject) override
	{
		ApplySkinSettingsAndHFVariant(InObject, OldSkinSettings);
	}
	//~End FToolCommandChange interface

private:
	// State to be used for applying the HF variant from the Texture skin property
	TSharedRef<FMetaHumanCharacterIdentity::FState> ReferenceFaceState;


	void ApplySkinSettingsAndHFVariant(UObject* InObject, const FMetaHumanCharacterSkinSettings& InSkinSettings)
	{
		UMetaHumanCharacter* MetaHumanCharacter = CastChecked<UMetaHumanCharacter>(InObject);
		UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();

		MetaHumanCharacterSubsystem->CommitSkinSettings(MetaHumanCharacter, InSkinSettings);

		// Copy the reference state and apply the HF variant
		TSharedRef<FMetaHumanCharacterIdentity::FState> NewState = MakeShared<FMetaHumanCharacterIdentity::FState>(*ReferenceFaceState);
		MetaHumanCharacterSubsystem->UpdateHFVariantFromSkinProperties(NewState, InSkinSettings.Skin);
		MetaHumanCharacterSubsystem->CommitFaceState(MetaHumanCharacter, NewState);

		UpdateSkinToolProperties(InSkinSettings);
	}
};

UInteractiveTool* UMetaHumanCharacterEditorSkinToolBuilder::BuildTool(const FToolBuilderState& InSceneState) const
{
	UToolTarget* Target = InSceneState.TargetManager->BuildFirstSelectedTargetable(InSceneState, GetTargetRequirements());
	check(Target);

	UMetaHumanCharacterEditorSkinTool* SkinTool = NewObject<UMetaHumanCharacterEditorSkinTool>(InSceneState.ToolManager);
	SkinTool->SetTarget(Target);

	return SkinTool;
}

void UMetaHumanCharacterEditorSkinToolProperties::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Override function to process EPropertyChangeType::ValueSet events for the edited properties
	Super::PostEditChangeProperty(PropertyChangedEvent);

	OnSkinPropertyValueSetDelegate.ExecuteIfBound(PropertyChangedEvent);
}

bool UMetaHumanCharacterEditorSkinToolProperties::CanEditChange(const FProperty* InProperty) const
{
	bool bIsEditable = Super::CanEditChange(InProperty);

	if (bIsEditable && InProperty != nullptr)
	{
		const FName PropertyName = InProperty->GetFName();

		if (PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorSkinToolProperties, Skin))
		{
			UMetaHumanCharacterEditorSkinTool* SkinTool = GetTypedOuter<UMetaHumanCharacterEditorSkinTool>();
			check(SkinTool);

			UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(SkinTool->GetTarget());
			check(Character);

			const bool bIsRequestingTextures = UMetaHumanCharacterEditorSubsystem::Get()->IsRequestingHighResolutionTextures(Character);
			bIsEditable = !bIsRequestingTextures;
		}

		// Disable rig-restricted properties (face/body texture indices, texture-attribute filter,
		// Texture Position Offset) when the character is rigged, since editing them would
		// regenerate the synthesized skin texture in ways that invalidate the rig.
		if (bIsEditable && IsRigRestrictedProperty(InProperty) && !CanEditRigRestrictedProperties())
		{
			bIsEditable = false;
		}

		// bShowHands needs a rigged character; the open-palm pose drives rigged hand bones, so it
		// is meaningless on an unrigged character.
		if (bIsEditable && PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorSkinToolProperties, bShowHands))
		{
			UMetaHumanCharacterEditorSkinTool* SkinTool = GetTypedOuter<UMetaHumanCharacterEditorSkinTool>();
			if (SkinTool)
			{
				UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(SkinTool->GetTarget());
				if (Character)
				{
					bIsEditable = UMetaHumanCharacterEditorSubsystem::Get()->GetRiggingState(Character) == EMetaHumanCharacterRigState::Rigged;
				}
			}
		}
	}

	return bIsEditable;
}

bool UMetaHumanCharacterEditorSkinToolProperties::CanEditRigRestrictedProperties() const
{
	UMetaHumanCharacterEditorSkinTool* SkinTool = GetTypedOuter<UMetaHumanCharacterEditorSkinTool>();
	if (!SkinTool)
	{
		return true;
	}

	UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(SkinTool->GetTarget());
	if (!Character)
	{
		return true;
	}

	const EMetaHumanCharacterRigState RiggingState = UMetaHumanCharacterEditorSubsystem::Get()->GetRiggingState(Character);
	return RiggingState == EMetaHumanCharacterRigState::Unrigged;
}


bool UMetaHumanCharacterEditorSkinToolProperties::IsRigRestrictedProperty(const FProperty* InProperty)
{
	if (!InProperty)
	{
		return false;
	}

	const FName PropertyName = InProperty->GetFName();

	// Properties that regenerate the synthesized skin texture in ways that affect the rig:
	// face/body texture indices, the texture-attribute filter (which drives FaceTextureIndex),
	// and Texture Position Offset (HighFrequencyDelta). Skin tone (U/V) is safe on a rigged
	// character so it is not in this list.
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, FaceTextureIndex) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, BodyTextureIndex) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorSkinToolProperties, bIsSkinFilterEnabled) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorSkinToolProperties, SkinFilterValues) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorSkinToolProperties, SkinFilterIndex) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterFaceEvaluationSettings, HighFrequencyDelta))
	{
		return true;
	}

	return false;
}

void UMetaHumanCharacterEditorSkinToolProperties::CopyTo(FMetaHumanCharacterSkinSettings& OutSkinSettings)
{
	OutSkinSettings.Skin = Skin;
	OutSkinSettings.Freckles = Freckles;
	OutSkinSettings.Accents = Accents;
	OutSkinSettings.DesiredTextureSourcesResolutions = DesiredTextureSourcesResolutions;
}

void UMetaHumanCharacterEditorSkinToolProperties::CopyFrom(const FMetaHumanCharacterSkinSettings& InSkinSettings)
{
	Skin = InSkinSettings.Skin;
	Freckles = InSkinSettings.Freckles;
	Accents = InSkinSettings.Accents;
	DesiredTextureSourcesResolutions = InSkinSettings.DesiredTextureSourcesResolutions;
}

void UMetaHumanCharacterEditorSkinToolProperties::CopyTo(FMetaHumanCharacterFaceEvaluationSettings& OutFaceEvaluationSettings)
{
	OutFaceEvaluationSettings = FaceEvaluationSettings;
}

void UMetaHumanCharacterEditorSkinToolProperties::CopyFrom(const FMetaHumanCharacterFaceEvaluationSettings& InFaceEvaluationSettings)
{
	FaceEvaluationSettings = InFaceEvaluationSettings;
}

void UMetaHumanCharacterEditorSkinTool::Setup()
{
	Super::Setup();

	SetToolDisplayName(LOCTEXT("SkinToolName", "Skin"));

	SkinToolProperties = NewObject<UMetaHumanCharacterEditorSkinToolProperties>(this);
	AddToolPropertySource(SkinToolProperties);

	UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	check(Character);

	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();

	PreShowHandsCameraFrame = Character->ViewportSettings.CameraFrame;
	bShowHandsPoseActivated = false;

	// Initialize the tool properties from the values stored in the Character
	FaceState = Subsystem->CopyFaceState(Character);
	PreviousSkinSettings = Character->SkinSettings;
	PreviousFaceEvaluationSettings = Character->FaceEvaluationSettings;

	SkinToolProperties->CopyFrom(Character->SkinSettings);
	SkinToolProperties->CopyFrom(Character->FaceEvaluationSettings);
	FilteredFaceTextureIndices.Reset();

	SkinToolProperties->SkinFilterValues.Reset();
	int32 NumTextureAttributes = Subsystem->GetFaceTextureAttributeMap().NumAttributes();
	for (int32 Idx = 0; Idx < NumTextureAttributes; ++Idx)
	{
		SkinToolProperties->SkinFilterValues.Push(int32(-1));
	}

	// Bind to the ValueSet event of the Skin Properties to fill in the undo stack
	SkinToolProperties->OnSkinPropertyValueSetDelegate.BindWeakLambda(this, [this](const FPropertyChangedEvent& PropertyChangedEvent)
		{
			if (UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target))
			{
				const FName PropertyName = PropertyChangedEvent.GetPropertyName();

				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();

				if (PropertyName == GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterFaceEvaluationSettings, HighFrequencyDelta))
				{
					// update the face settings only if they differ
					FMetaHumanCharacterFaceEvaluationSettings NewFaceEvaluationSettings;
					SkinToolProperties->CopyTo(NewFaceEvaluationSettings);

					if (Character->FaceEvaluationSettings == NewFaceEvaluationSettings)
					{
						return;
					}
					if ((PropertyChangedEvent.ChangeType & (EPropertyChangeType::ValueSet | EPropertyChangeType::ResetToDefault)) != 0u && ((PropertyChangedEvent.ChangeType & EPropertyChangeType::Interactive) == 0u))
					{
						Subsystem->CommitFaceEvaluationSettings(Character, NewFaceEvaluationSettings);

						FOnSettingsUpdateDelegate OnSettingsUpdateDelegate;
						OnSettingsUpdateDelegate.BindWeakLambda(this, [this](TWeakObjectPtr<UInteractiveToolManager> ToolManager, const FMetaHumanCharacterFaceEvaluationSettings& FaceEvaluationSettings)
							{
								UpdateSkinToolProperties(ToolManager, FaceEvaluationSettings);
							});

						TUniquePtr<FMetaHumanCharacterEditorFaceEvaluationSettingsCommandChange> CommandChange = MakeUnique<FMetaHumanCharacterEditorFaceEvaluationSettingsCommandChange>(Character, PreviousFaceEvaluationSettings, OnSettingsUpdateDelegate, GetToolManager());
						GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("SkinToolVertexDeltaCommandChange", "Face Blend Tool Vertex Delta"));
						PreviousFaceEvaluationSettings = NewFaceEvaluationSettings;
					}
					else
					{
						Subsystem->ApplyFaceEvaluationSettings(Character, NewFaceEvaluationSettings);
					}
				}
				else
				{
					bool bIsSkinModified = false;
					bool bIsTextureModified = false;
					// When the reset to default button is clicked in the details panel ChangeType will have both ValueSet and ResetToDefault bits set
					if ((PropertyChangedEvent.ChangeType & (EPropertyChangeType::ValueSet | EPropertyChangeType::ResetToDefault)) != 0u)
					{
						bIsSkinModified = true;
						// The Skin Texture property is handled differently since we need to update both texture and face state
						if (PropertyName == GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, FaceTextureIndex) ||
							PropertyName == GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, BodyTextureIndex))
						{
							bIsTextureModified = true;
						}
					}
					else
					{
						// One of the texture source resolutions changed
						const FProperty* TextureSourcesProperty = UMetaHumanCharacterEditorSkinToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorSkinToolProperties, DesiredTextureSourcesResolutions));
						if (PropertyChangedEvent.Property->GetOwnerProperty() == TextureSourcesProperty)
						{
							bIsSkinModified = true;
						}

						// Mark the skin as modified if accent regions or freckles have changed
						if (PropertyChangedEvent.Property->GetOwnerStruct() == FMetaHumanCharacterAccentRegionProperties::StaticStruct() ||
							PropertyChangedEvent.Property->GetOwnerStruct() == FMetaHumanCharacterFrecklesProperties::StaticStruct())
						{
							bIsSkinModified = true;
						}
					}

					if (bIsSkinModified)
					{
						// Add finished changes in Skin Properties to the undo stack
						FMetaHumanCharacterSkinSettings CurrentSkinSettings = Character->SkinSettings;
						SkinToolProperties->CopyTo(CurrentSkinSettings);

						// Add the undo command
						if (bIsTextureModified)
						{
							TUniquePtr<FMetaHumanCharacterEditorSkinTextureCommandChange> CommandChange =
								MakeUnique<FMetaHumanCharacterEditorSkinTextureCommandChange>(PreviousSkinSettings, CurrentSkinSettings, FaceState.ToSharedRef(), GetToolManager());
							GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("SkinTextureCommandChange", "Edit Skin Texture"));
							bSkinTextureWasModified = true;
						}
						else
						{
							TUniquePtr<FMetaHumanCharacterEditorSkinToolCommandChange> CommandChange =
								MakeUnique<FMetaHumanCharacterEditorSkinToolCommandChange>(PreviousSkinSettings, CurrentSkinSettings, GetToolManager());
							GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("SkinToolCommandChange", "Edit Skin"));
						}

						PreviousSkinSettings = CurrentSkinSettings;
						bActorWasModified = true;

						UpdateSkinState();
					}
				}
			}
		});

	SkinToolProperties->WatchProperty(SkinToolProperties->Skin.U,
		[this](float)
		{
			UpdateSkinSynthesizedTexture();
		});

	SkinToolProperties->WatchProperty(SkinToolProperties->Skin.V,
		[this](float)
		{
			UpdateSkinSynthesizedTexture();
		});

	SkinToolProperties->WatchProperty(SkinToolProperties->Skin.FaceTextureIndex,
		[this](int32)
		{
			UpdateSkinSynthesizedTexture();
		});

	SkinToolProperties->WatchProperty(SkinToolProperties->SkinFilterIndex,
		[this](int32)
		{
			UpdateFaceTextureFromFilterIndex();
		});

	SkinToolProperties->WatchProperty(SkinToolProperties->bIsSkinFilterEnabled,
		[this](bool bInIsSkinFilterEnabled)
		{
			SetEnableSkinFilter(bInIsSkinFilterEnabled);
		});

	SkinToolProperties->WatchProperty(SkinToolProperties->SkinFilterValues,
		[this](const TArray<int32>& Values)
		{
			SetEnableSkinFilter(SkinToolProperties->bIsSkinFilterEnabled);
		});

	SkinToolProperties->WatchProperty(SkinToolProperties->Skin.BodyTextureIndex,
		[this](int32)
		{
			UpdateSkinSynthesizedTexture();
		});

	SkinToolProperties->WatchProperty(SkinToolProperties->Skin.Roughness,
		[this](float)
		{
			UpdateSkinState();
		});

	// Palm Props
	SkinToolProperties->WatchProperty(SkinToolProperties->Skin.PalmLightness,
		[this](float)
		{
			UpdateSkinState();
		});

	SkinToolProperties->WatchProperty(SkinToolProperties->Skin.PalmTint,
		[this](float)
		{
			UpdateSkinState();
		});

	SkinToolProperties->WatchProperty(SkinToolProperties->Skin.PalmCavityDarkness,
		[this](float)
		{
			UpdateSkinState();
		});

	// Fingernail Props
	SkinToolProperties->WatchProperty(SkinToolProperties->Skin.FingernailTintColor,
		[this](FLinearColor)
		{
			UpdateSkinState();
		});

	SkinToolProperties->WatchProperty(SkinToolProperties->Skin.FingernailTintIntensity,
		[this](float)
		{
			UpdateSkinState();
		});

	SkinToolProperties->WatchProperty(SkinToolProperties->Skin.FingernailMetallic,
		[this](float)
		{
			UpdateSkinState();
		});

	SkinToolProperties->WatchProperty(SkinToolProperties->Skin.FingernailRoughness,
		[this](float)
		{
			UpdateSkinState();
		});

	// Toenail Props
	SkinToolProperties->WatchProperty(SkinToolProperties->Skin.ToenailTintColor,
		[this](FLinearColor)
		{
			UpdateSkinState();
		});

	SkinToolProperties->WatchProperty(SkinToolProperties->Skin.ToenailTintIntensity,
		[this](float)
		{
			UpdateSkinState();
		});

	SkinToolProperties->WatchProperty(SkinToolProperties->Skin.ToenailMetallic,
		[this](float)
		{
			UpdateSkinState();
		});

	SkinToolProperties->WatchProperty(SkinToolProperties->Skin.ToenailRoughness,
		[this](float)
		{
			UpdateSkinState();
		});

	// Update the max values of the face texture slider based on the texture model
	FProperty* FaceTextureIndexProperty = FMetaHumanCharacterSkinProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, FaceTextureIndex));
	FaceTextureIndexProperty->SetMetaData("UIMax", FString::FromInt(Subsystem->GetMaxHighFrequencyIndex() - 1));
	FaceTextureIndexProperty->SetMetaData("ClampMax", FString::FromInt(Subsystem->GetMaxHighFrequencyIndex() - 1));

	SkinToolProperties->WatchProperty(SkinToolProperties->bShowHands,
		[this](bool bInShowHands) { OnShowHandsChanged(bInShowHands); });

	SkinToolProperties->RestoreProperties(this, FString::Printf(TEXT("MetaHumanCharacterEditorSkinTool_%s"), *Character->GetName()));
	SetEnableSkinFilter(SkinToolProperties->bIsSkinFilterEnabled);

	// Updates the cached parameters of all property watchers to avoid triggering the update functions when the tool starts
	SkinToolProperties->SilentUpdateWatched();

	// Auto select skin preview if in topology mode
	if (Character->PreviewMaterialType == EMetaHumanCharacterSkinPreviewMaterial::Default)
	{
		Subsystem->UpdateCharacterPreviewMaterial(Character, EMetaHumanCharacterSkinPreviewMaterial::Editable);
	}

	// Post a warning to the mode toolkit when the character is rigged so the user knows that
	// face/body texture selection is locked. Keep it in sync with the character's rigging state
	// for the lifetime of the tool.
	RiggingStateChangedHandle = Character->OnRiggingStateChanged.AddUObject(this, &UMetaHumanCharacterEditorSkinTool::UpdateRiggedWarning);
	UpdateRiggedWarning();

	SetShowHandsPose(SkinToolProperties->bShowHands);

	// Saving the character's package re-instances the preview actor's anim instances, which wipes
	// any pose driven by SetShowHandsPose. Re-apply the show-hands pose after the save so the
	// open-palm pose persists across saves instead of silently snapping back to the default pose.
	PackageSavedHandle = UPackage::PackageSavedWithContextEvent.AddWeakLambda(this,
		[this](const FString& PackageFileName, UPackage* Package, FObjectPostSaveContext ObjectSaveContext)
		{
			UMetaHumanCharacter* SavedCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
			if (SavedCharacter && SavedCharacter->GetPackage() == Package)
			{
				SetShowHandsPose(SkinToolProperties->bShowHands);
			}
		});
}

void UMetaHumanCharacterEditorSkinTool::Shutdown(EToolShutdownType InShutdownType)
{
	UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	check(Character);

	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();

	SkinToolProperties->SaveProperties(this, FString::Printf(TEXT("MetaHumanCharacterEditorSkinTool_%s"), *Character->GetName()));

	if (bActorWasModified)
	{
		FMetaHumanCharacterSkinSettings CurrentSkinSettings = Character->SkinSettings;
		SkinToolProperties->CopyTo(CurrentSkinSettings);

		Subsystem->CommitSkinSettings(Character, CurrentSkinSettings);
		if (bSkinTextureWasModified)
		{
			Subsystem->CommitFaceState(Character, Subsystem->GetFaceState(Character));
		}

		// Add the undo command
		const FText CommandChangeDescription = FText::Format(LOCTEXT("SkinEditingCommandChangeTransaction", "{0} {1}"),
			UEnum::GetDisplayValueAsText(InShutdownType),
			GetCommandChangeDescription());

		// OriginalSkinSettings were either set when
		// - tool opened (Cancel)
		// - in the statement above to the latest settings (Accept)
		// in both cases we add a command from PreviousSkinSettings -> OriginalSkinSettings
		if (bSkinTextureWasModified)
		{
			TUniquePtr<FMetaHumanCharacterEditorSkinTextureCommandChange> CommandChange =
				MakeUnique<FMetaHumanCharacterEditorSkinTextureCommandChange>(PreviousSkinSettings, CurrentSkinSettings, FaceState.ToSharedRef(), GetToolManager());
			GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), CommandChangeDescription);
		}
		else
		{
			TUniquePtr<FMetaHumanCharacterEditorSkinToolCommandChange> CommandChange =
				MakeUnique<FMetaHumanCharacterEditorSkinToolCommandChange>(PreviousSkinSettings, CurrentSkinSettings, GetToolManager());
			GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character,
				MoveTemp(CommandChange), CommandChangeDescription);
		}
	}

	// Cancel the pending hands-focus ticker so it cannot fire after the tool has shut down.
	if (ShowHandsPoseFocusTickerHandle.IsSet())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ShowHandsPoseFocusTickerHandle.GetValue());
		ShowHandsPoseFocusTickerHandle.Reset();
	}

	SetShowHandsPose(false);

	if (bShowHandsPoseActivated)
	{
		Subsystem->OnCameraFocusRequested(Character).ExecuteIfBound(Character, PreShowHandsCameraFrame);
	}

	// Stop listening for rigging state changes and clear any warning we may have posted.
	Character->OnRiggingStateChanged.Remove(RiggingStateChangedHandle);
	RiggingStateChangedHandle.Reset();

	UPackage::PackageSavedWithContextEvent.Remove(PackageSavedHandle);
	PackageSavedHandle.Reset();
	GetToolManager()->DisplayMessage(FText::GetEmpty(), EToolMessageLevel::UserWarning);
}

void UMetaHumanCharacterEditorSkinTool::UpdateRiggedWarning()
{
	UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	if (Character && SkinToolProperties)
	{
		const bool bIsRigged = UMetaHumanCharacterEditorSubsystem::Get()->GetRiggingState(Character) == EMetaHumanCharacterRigState::Rigged;
		if (!bIsRigged && SkinToolProperties->bShowHands)
		{
			SkinToolProperties->bShowHands = false;
			SetShowHandsPose(false);
		}
	}

	if (!SkinToolProperties || SkinToolProperties->CanEditRigRestrictedProperties())
	{
		GetToolManager()->DisplayMessage(FText::GetEmpty(), EToolMessageLevel::UserWarning);
		return;
	}

	GetToolManager()->DisplayMessage(
		LOCTEXT("SkinEditRiggedWarningMessage", "Face and body texture selection are locked while the character is rigged. Unrig to modify them."),
		EToolMessageLevel::UserWarning);
}

void UMetaHumanCharacterEditorSkinTool::UpdateSkinState() const
{
	UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	check(Character);

	FMetaHumanCharacterSkinSettings CurrentSkinSettings = Character->SkinSettings;
	SkinToolProperties->CopyTo(CurrentSkinSettings);

	UMetaHumanCharacterEditorSubsystem::Get()->ApplySkinSettings(Character, CurrentSkinSettings);
}

void UMetaHumanCharacterEditorSkinTool::OnShowHandsChanged(bool bInShowHands)
{
	// Cancel any in-flight focus ticker from a previous toggle
	if (ShowHandsPoseFocusTickerHandle.IsSet())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ShowHandsPoseFocusTickerHandle.GetValue());
		ShowHandsPoseFocusTickerHandle.Reset();
	}

	UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);

	if (!IsValid(Character))
	{
		return;
	}

	// Capture the camera frame the character was on before entering the show-hands pose
	// so toggling the pose off (or shutting the tool down) can return to it.
	if (bInShowHands)
	{
		PreShowHandsCameraFrame = Character->ViewportSettings.CameraFrame;
	}

	SetShowHandsPose(bInShowHands);

	if (!bInShowHands)
	{
		UMetaHumanCharacterEditorSubsystem::Get()->OnCameraFocusRequested(Character).ExecuteIfBound(Character, PreShowHandsCameraFrame);
		return;
	}

	bShowHandsPoseActivated = true;

	// Wait for the show-hands pose animation to finish playing before focusing the camera, so the
	// hand bones have moved into the open-palm pose by the time we compute the focus bounds.
	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	check(Settings);
	UAnimSequence* PalmPoseAnim = Settings->ShowHandsPose.LoadSynchronous();
	const float PalmPoseAnimPlayLength = PalmPoseAnim ? PalmPoseAnim->GetPlayLength() : 0.0f;
	constexpr float MinSettleDurationSeconds = 0.3f;
	const float SettleDurationSeconds = FMath::Max(PalmPoseAnimPlayLength, MinSettleDurationSeconds);

	TWeakObjectPtr<UMetaHumanCharacter> WeakCharacter = Character;
	TWeakObjectPtr<UMetaHumanCharacterEditorSkinTool> WeakThis = this;
	ShowHandsPoseFocusTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[WeakThis, WeakCharacter, SettleDurationSeconds, ElapsedSeconds = 0.0f](float DeltaTime) mutable -> bool
		{
			UMetaHumanCharacterEditorSkinTool* StrongThis = WeakThis.Get();
			UMetaHumanCharacter* StrongCharacter = WeakCharacter.Get();
			if (!StrongThis || !StrongCharacter)
			{
				return false;
			}

			ElapsedSeconds += DeltaTime;
			if (ElapsedSeconds < SettleDurationSeconds)
			{
				return true;
			}

			if (UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get())
			{
				Subsystem->OnCameraFocusRequested(StrongCharacter).ExecuteIfBound(StrongCharacter, EMetaHumanCharacterCameraFrame::Hands);
			}
			StrongThis->ShowHandsPoseFocusTickerHandle.Reset();
			return false;
		}));
}

void UMetaHumanCharacterEditorSkinTool::SetShowHandsPose(bool bShowHandsPose)
{
	UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	check(Character);

	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	if (!Subsystem)
	{
		return;
	}

	AMetaHumanInvisibleDrivingActor* DrivingActor = Subsystem->GetInvisibleDrivingActor(Character);
	if (!IsValid(DrivingActor))
	{
		return;
	}

	if (!bShowHandsPose)
	{
		// Only restore when a pre-pose snapshot was cached. Without this guard, calling
		// SetShowHandsPose(false) on a fresh tool start (or any state where the pose was never
		// activated) would clear whatever animation the preview was already driving.
		if (bHasPreShowHandsAnim)
		{
			DrivingActor->SetAnimation(PreShowHandsFaceAnim.Get(), PreShowHandsBodyAnim.Get());
			PreShowHandsFaceAnim.Reset();
			PreShowHandsBodyAnim.Reset();
			bHasPreShowHandsAnim = false;
		}
		return;
	}

	// Show hands pose only makes sense on a rigged character; the open-palm pose is animated through
	// the rigged hand bones, so requesting it on an unrigged character yields no visible change.
	if (Subsystem->GetRiggingState(Character) != EMetaHumanCharacterRigState::Rigged)
	{
		return;
	}

	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	check(Settings);

	UAnimSequence* PalmPoseAnim = Settings->ShowHandsPose.LoadSynchronous();
	if (!PalmPoseAnim)
	{
		return;
	}

	// Cache the animation currently driving the character so toggling the pose off can restore it.
	// Guard against re-capture: re-entrant calls (e.g. from the PackageSaved handler that re-applies
	// the pose) would otherwise overwrite the cache with the palm pose itself.
	if (!bHasPreShowHandsAnim)
	{
		if (UMetaHumanCharacterAnimInstance* AnimInstance = DrivingActor->GetPreviewAnimInstance())
		{
			// UMetaHumanCharacterAnimInstance::SetAnimation packs body into PrimaryAnimation and
			// face into SecondaryAnimation when both are set, and stores face alone in Primary
			// otherwise. Reverse that mapping to recover the original face/body pair.
			if (AnimInstance->SecondaryAnimation)
			{
				PreShowHandsBodyAnim = AnimInstance->PrimaryAnimation;
				PreShowHandsFaceAnim = AnimInstance->SecondaryAnimation;
			}
			else
			{
				PreShowHandsBodyAnim = nullptr;
				PreShowHandsFaceAnim = AnimInstance->PrimaryAnimation;
			}
			bHasPreShowHandsAnim = true;
		}
	}

	DrivingActor->InitPreviewAnimInstance();
	DrivingActor->SetAnimation(PalmPoseAnim, PalmPoseAnim);
	DrivingActor->PlayAnimation();
}

const FText UMetaHumanCharacterEditorSkinTool::GetCommandChangeDescription() const
{
	return LOCTEXT("FaceSkinToolCommandChange", "Face Skin Tool");
}

bool UMetaHumanCharacterEditorSkinTool::UpdateSkinSynthesizedTexture()
{
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	check(MetaHumanCharacter);

	bool bCanUpdate = true;

	if (MetaHumanCharacter->HasHighResolutionTextures())
	{
		const FText Message = LOCTEXT("PromptHighResTexture", "This MetaHuman has high resolution textures assigned to it, making this change will discard the current texture and replace it with a lower resolution one. Do you want to continue?");

		const EAppReturnType::Type Reply = FMessageDialog::Open(EAppMsgType::YesNo, Message);
		bCanUpdate = (Reply == EAppReturnType::Yes);
	}

	if (bCanUpdate)
	{
		const bool bHadHighResolutionTextures = MetaHumanCharacter->HasHighResolutionTextures();

		UpdateSkinState();

		if (bHadHighResolutionTextures)
		{
			// If we can update but the character had high resolution textures before the update, it means a dialog asking the user
			// to proceed was displayed. In this case, for some reason, the ValueSet event is not emitted so we are emitting one
			// here to make sure the skin tool registers the change and creates a transaction for it
			const FName SkinPropertyName = GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorSkinToolProperties, Skin);
			FProperty* SkinProperty = UMetaHumanCharacterEditorSkinToolProperties::StaticClass()->FindPropertyByName(SkinPropertyName);
			FPropertyChangedEvent ValueSetEvent{ SkinProperty, EPropertyChangeType::ValueSet };
			SkinToolProperties->PostEditChangeProperty(ValueSetEvent);
		}
	}
	else
	{
		// Restore the previous skin texture parameters
		SkinToolProperties->Skin = PreviousSkinSettings.Skin;
		SkinToolProperties->SilentUpdateWatched();
	}


	return bCanUpdate;
}

void UMetaHumanCharacterEditorSkinTool::UpdateSkinToolProperties(TWeakObjectPtr<UInteractiveToolManager> InToolManager, const FMetaHumanCharacterFaceEvaluationSettings& InFaceEvaluationSettings)
{
	if (InToolManager.IsValid())
	{
		SkinToolProperties->CopyFrom(InFaceEvaluationSettings);
		SkinToolProperties->SilentUpdateWatched();

		// Restore the PreviousSkinSettings of the tool to what we are applying so that
		// new commands are created with the correct previous settings
		PreviousFaceEvaluationSettings = InFaceEvaluationSettings;
	}
}

void UMetaHumanCharacterEditorSkinTool::UpdateFaceTextureFromFilterIndex()
{
	if (FilteredFaceTextureIndices.IsValid())
	{
		const int32 FaceTextureIndex = FilteredFaceTextureIndices->ConvertFilterIndexToTextureIndex(SkinToolProperties->SkinFilterIndex);

		if (FaceTextureIndex >= 0 && FaceTextureIndex < UMetaHumanCharacterEditorSubsystem::Get()->GetMaxHighFrequencyIndex())
		{
			SkinToolProperties->Skin.FaceTextureIndex = FaceTextureIndex;
		}
	}
}

void UMetaHumanCharacterEditorSkinTool::SetEnableSkinFilter(bool bInEnableSkinFilter)
{
	if (bInEnableSkinFilter)
	{
		UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
		
		FilteredFaceTextureIndices = MakeShared<FMetaHumanFilteredFaceTextureIndices>(Subsystem->GetFaceTextureAttributeMap(), SkinToolProperties->SkinFilterValues);
		SkinToolProperties->SkinFilterIndex = FilteredFaceTextureIndices->ConvertTextureIndexToFilterIndex(SkinToolProperties->Skin.FaceTextureIndex);

		// Update the max values of the skin filter slider
		FProperty* SkinFilterIndexProperty = UMetaHumanCharacterEditorSkinToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorSkinToolProperties, SkinFilterIndex));
		SkinFilterIndexProperty->SetMetaData("UIMax", FString::FromInt(FilteredFaceTextureIndices->Num() - 1));
		SkinFilterIndexProperty->SetMetaData("ClampMax", FString::FromInt(FilteredFaceTextureIndices->Num() - 1));
	}
	else
	{
		FilteredFaceTextureIndices.Reset();
	}
}

bool UMetaHumanCharacterEditorSkinTool::IsFilteredFaceTextureIndicesValid() const
{
	return (FilteredFaceTextureIndices.IsValid() && FilteredFaceTextureIndices->Num() > 0);
}

#undef LOCTEXT_NAMESPACE