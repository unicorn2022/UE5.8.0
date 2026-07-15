// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorImportTools.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "MetaHumanCharacterEditorActor.h"
#include "MetaHumanCharacterEditorCommands.h"
#include "MetaHumanCharacterEditorToolTargetUtil.h"
#include "MetaHumanIdentity.h"
#include "MetaHumanIdentityParts.h"
#include "MetaHumanIdentityPose.h"
#include "MetaHumanSDKEditor.h"
#include "MetaHumanCharacterAnalytics.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "InteractiveToolManager.h"
#include "ToolTargetManager.h"
#include "DNAUtils.h"
#include "Editor/EditorEngine.h"
#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditorConformTool"

extern UNREALED_API UEditorEngine* GEditor;

class FImportToolStateCommandChange : public FToolCommandChange
{
public:

	FImportToolStateCommandChange(
		TSharedRef<const FMetaHumanCharacterIdentity::FState> InOldFaceState,
		TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InOldBodyState,
		TNotNull<UMetaHumanCharacter*> InCharacter,
		TNotNull<UInteractiveToolManager*> InToolManager)
		: OldFaceState{ InOldFaceState }
		, NewFaceState{ UMetaHumanCharacterEditorSubsystem::Get()->CopyFaceState(InCharacter) }
		, OldBodyState{ InOldBodyState }
		, NewBodyState{ UMetaHumanCharacterEditorSubsystem::Get()->CopyBodyState(InCharacter) }
		, ToolManager{ InToolManager }
	{
	}

	//~Begin FToolCommandChange interface
	virtual void Apply(UObject* InObject) override
	{
		UMetaHumanCharacter* Character = CastChecked<UMetaHumanCharacter>(InObject);
		UMetaHumanCharacterEditorSubsystem::Get()->CommitFaceState(Character, NewFaceState);
		UMetaHumanCharacterEditorSubsystem::Get()->CommitBodyState(Character, NewBodyState);
	}

	virtual void Revert(UObject* InObject) override
	{
		UMetaHumanCharacter* Character = CastChecked<UMetaHumanCharacter>(InObject);
		UMetaHumanCharacterEditorSubsystem::Get()->CommitFaceState(Character, OldFaceState);
		UMetaHumanCharacterEditorSubsystem::Get()->CommitBodyState(Character, OldBodyState);
	}

	virtual bool HasExpired(UObject* InObject) const override
	{
		// If the ToolManager is not valid anymore it means the asset editor was closed so mark the transaction as expired
		return !ToolManager.IsValid();
	}
	//~End FToolCommandChange interface


protected:

	TSharedRef<const FMetaHumanCharacterIdentity::FState> OldFaceState;
	TSharedRef<const FMetaHumanCharacterIdentity::FState> NewFaceState;
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> OldBodyState;
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> NewBodyState;

	TWeakObjectPtr<UInteractiveToolManager> ToolManager;

};

class FImportToolDNACommandChange : public FToolCommandChange
{
public:

	FImportToolDNACommandChange(
		const TArray<uint8>& InOldFaceDNABuffer,
		const TArray<uint8>& InOldBodyDNABuffer,
		TSharedRef<const FMetaHumanCharacterIdentity::FState> InOldFaceState,
		TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InOldBodyState,
		TNotNull<UMetaHumanCharacter*> InCharacter,
		TNotNull<UInteractiveToolManager*> InToolManager)
		: OldFaceDNABuffer{ InOldFaceDNABuffer }
		, OldBodyDNABuffer{ InOldBodyDNABuffer }
		, NewFaceDNABuffer{ InCharacter->GetFaceDNABuffer() }
		, NewBodyDNABuffer{ InCharacter->GetBodyDNABuffer() }
		, OldFaceState{ InOldFaceState }
		, OldBodyState{ InOldBodyState }
		, NewFaceState{ UMetaHumanCharacterEditorSubsystem::Get()->CopyFaceState(InCharacter) }
		, NewBodyState{ UMetaHumanCharacterEditorSubsystem::Get()->CopyBodyState(InCharacter) }
		, ToolManager{ InToolManager }
	{
	}

	//~Begin FToolCommandChange interface
	virtual void Apply(UObject* InObject) override
	{
		ApplyChange(InObject, NewFaceDNABuffer, NewBodyDNABuffer, NewFaceState, NewBodyState);
	}

	virtual void Revert(UObject* InObject) override
	{
		ApplyChange(InObject, OldFaceDNABuffer, OldBodyDNABuffer, OldFaceState, OldBodyState);
	}

	virtual bool HasExpired(UObject* InObject) const override
	{
		// If the ToolManager is not valid anymore it means the asset editor was closed so mark the transaction as expired
		return !ToolManager.IsValid();
	}
	//~End FToolCommandChange interface


protected:

	void ApplyChange(UObject* InObject, const TArray<uint8>& InFaceDNABuffer, const TArray<uint8>& InBodyDNABuffer, TSharedRef<const FMetaHumanCharacterIdentity::FState> InFaceState, TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InBodyState)
	{
		UMetaHumanCharacter* Character = CastChecked<UMetaHumanCharacter>(InObject);

		// if an empty buffer, remove the rig from the character (special case)
		if (InBodyDNABuffer.IsEmpty())
		{
			UMetaHumanCharacterEditorSubsystem::Get()->RemoveBodyRig(Character);
		}
		else
		{
			TArray<uint8> BufferCopy;
			BufferCopy.SetNumUninitialized(InBodyDNABuffer.Num());
			FMemory::Memcpy(BufferCopy.GetData(), InBodyDNABuffer.GetData(), InBodyDNABuffer.Num());
			constexpr bool bImportingAsFixedBodyType = true;
			UMetaHumanCharacterEditorSubsystem::Get()->CommitBodyDNA(Character, ReadDNAFromBuffer(&BufferCopy, EDNADataLayer::All).ToSharedRef(), bImportingAsFixedBodyType);
		}

		// if an empty buffer, remove the rig from the character (special case)
		if (InFaceDNABuffer.IsEmpty())
		{
			UMetaHumanCharacterEditorSubsystem::Get()->RemoveFaceRig(Character);
		}
		else
		{
			TArray<uint8> BufferCopy;
			BufferCopy.SetNumUninitialized(InFaceDNABuffer.Num());
			FMemory::Memcpy(BufferCopy.GetData(), InFaceDNABuffer.GetData(), InFaceDNABuffer.Num());
			UMetaHumanCharacterEditorSubsystem::Get()->CommitFaceDNA(Character, ReadDNAFromBuffer(&BufferCopy, EDNADataLayer::All).ToSharedRef());
		}

		// reset the face state
		UMetaHumanCharacterEditorSubsystem::Get()->CommitBodyState(Character, InBodyState, UMetaHumanCharacterEditorSubsystem::EBodyMeshUpdateMode::Minimal);
		UMetaHumanCharacterEditorSubsystem::Get()->CommitFaceState(Character, InFaceState);
	}

	TArray<uint8> OldFaceDNABuffer;
	TArray<uint8> OldBodyDNABuffer;
	TArray<uint8> NewFaceDNABuffer;
	TArray<uint8> NewBodyDNABuffer;

	TSharedRef<const FMetaHumanCharacterIdentity::FState> OldFaceState;
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> OldBodyState;
	TSharedRef<const FMetaHumanCharacterIdentity::FState> NewFaceState;
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> NewBodyState;

	TWeakObjectPtr<UInteractiveToolManager> ToolManager;

};

bool UMetaHumanCharacterEditorImportFromDNAToolBuilder::CanBuildTool(const FToolBuilderState& InSceneState) const
{
	return Super::CanBuildTool(InSceneState) && !IsCharacterRigged(InSceneState);
}

UInteractiveTool* UMetaHumanCharacterEditorImportFromDNAToolBuilder::BuildTool(const FToolBuilderState& InSceneState) const
{
	UToolTarget* Target = InSceneState.TargetManager->BuildFirstSelectedTargetable(InSceneState, GetTargetRequirements());
	check(Target);

	UMetaHumanCharacterEditorImportFromDNATool* ImportFromDNATool = NewObject<UMetaHumanCharacterEditorImportFromDNATool>(InSceneState.ToolManager);
	ImportFromDNATool->SetTarget(Target);

	return ImportFromDNATool;
}

bool UMetaHumanCharacterEditorImportFromIdentityToolBuilder::CanBuildTool(const FToolBuilderState& InSceneState) const
{
	return Super::CanBuildTool(InSceneState) && !IsCharacterRigged(InSceneState);
}

UInteractiveTool* UMetaHumanCharacterEditorImportFromIdentityToolBuilder::BuildTool(const FToolBuilderState& InSceneState) const
{
	UToolTarget* Target = InSceneState.TargetManager->BuildFirstSelectedTargetable(InSceneState, GetTargetRequirements());
	check(Target);

	UMetaHumanCharacterEditorImportFromIdentityTool* ImportFromIdentityTool = NewObject<UMetaHumanCharacterEditorImportFromIdentityTool>(InSceneState.ToolManager);
	ImportFromIdentityTool->SetTarget(Target);

	return ImportFromIdentityTool;
}

bool UMetaHumanCharacterEditorImportFromTemplateToolBuilder::CanBuildTool(const FToolBuilderState& InSceneState) const
{
	return Super::CanBuildTool(InSceneState) && !IsCharacterRigged(InSceneState);
}

UInteractiveTool* UMetaHumanCharacterEditorImportFromTemplateToolBuilder::BuildTool(const FToolBuilderState& InSceneState) const
{
	UToolTarget* Target = InSceneState.TargetManager->BuildFirstSelectedTargetable(InSceneState, GetTargetRequirements());
	check(Target);

	UMetaHumanCharacterEditorImportFromTemplateTool* ImportFromTemplateTool = NewObject<UMetaHumanCharacterEditorImportFromTemplateTool>(InSceneState.ToolManager);
	ImportFromTemplateTool->SetTarget(Target);

	return ImportFromTemplateTool;
}

void UMetaHumanCharacterImportSubToolBase::DisplayConformError(const FText& ErrorMessageText) const
{
	UInteractiveTool* OwnerTool = GetTypedOuter<UInteractiveTool>();
	check(OwnerTool);

	OwnerTool->GetToolManager()->DisplayMessage(ErrorMessageText, EToolMessageLevel::UserError);
	FMessageLog(UE::MetaHuman::MessageLogName).Error(ErrorMessageText);
	FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, false);
}

void UMetaHumanCharacterImportFromDNAProperties::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromDNAProperties, bAlignScale) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromDNAProperties, bAlignRotation) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromDNAProperties, bAlignTranslation))
	{
		EAlignmentOptions AlignmentOptions = EAlignmentOptions::None;
		if (bAlignScale && bAlignRotation && bAlignTranslation)
		{
			AlignmentOptions = EAlignmentOptions::ScalingRotationTranslation;
		}
		else if (bAlignRotation && bAlignTranslation)
		{
			AlignmentOptions = EAlignmentOptions::RotationTranslation;
		}
		else if (bAlignScale && bAlignTranslation)
		{
			AlignmentOptions = EAlignmentOptions::ScalingTranslation;
		}
		else if (bAlignTranslation)
		{
			AlignmentOptions = EAlignmentOptions::Translation;
		}

		ImportOptions.AlignmentOptions = AlignmentOptions;
	}
}

bool UMetaHumanCharacterImportFromDNAProperties::CanImportMesh() const
{
	if (ImportOptions.bImportWholeRig)
	{
		return !DNABody.FilePath.IsEmpty() || !DNAHead.FilePath.IsEmpty();
	}
	
	return !DNABody.FilePath.IsEmpty();
}

void UMetaHumanCharacterImportFromDNAProperties::ImportMesh()
{
	const float ImportWorkProgress = 2.0f;
	FScopedSlowTask ImportTask(ImportWorkProgress, LOCTEXT("ImportMeshDnaTaskMessage", "Importing Mesh from DNA"));
	ImportTask.MakeDialog();

	TNotNull<UMetaHumanCharacterEditorImportTool*> OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorImportTool>();
	TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	// Snapshot state before any mutations so the undo CommandChange captures the correct OldState
	OwnerTool->UpdateOriginalState();
	TArray<FVector3f> OutVertices;
	TSharedPtr<IDNAReader> BodyDNAReader = ReadDNAFromFile(DNABody.FilePath, EDNADataLayer::All);
	TSharedPtr<IDNAReader> HeadDNAReader = ReadDNAFromFile(DNAHead.FilePath, EDNADataLayer::All);
	
	EImportErrorCode ErrorCode = EImportErrorCode::Success;
	if (!DNABody.FilePath.IsEmpty() && !DNAHead.FilePath.IsEmpty())
	{
		if (BodyDNAReader.IsValid() && HeadDNAReader.IsValid())
		{
			ErrorCode = Subsystem->GetMeshForBodyConforming(Character, BodyDNAReader.ToSharedRef(), HeadDNAReader, OutVertices);
		}
		else
		{
			ErrorCode = EImportErrorCode::InvalidInputData;
		}
	}
	else if (!DNABody.FilePath.IsEmpty())
	{
		if (BodyDNAReader.IsValid())
		{
			ErrorCode = Subsystem->GetMeshForBodyConforming(Character, BodyDNAReader.ToSharedRef(), nullptr, OutVertices);
		}
		else
		{
			ErrorCode = EImportErrorCode::InvalidInputData;
		}
	}
	else
	{
		ErrorCode = EImportErrorCode::InvalidInputData;
	}

	const bool bImportMeshSucceeded = (ErrorCode == EImportErrorCode::Success)
		&& Subsystem->SetBodyMesh(Character, OutVertices, BodyParams.bAutoRigHelperJoints);
	if (bImportMeshSucceeded)
	{
		// Add command change to undo stack
		TUniquePtr<FImportToolStateCommandChange> CommandChange = MakeUnique<FImportToolStateCommandChange>(
			OwnerTool->GetOriginalFaceState(),
			OwnerTool->GetOriginalBodyState(),
			Character,
			OwnerTool->GetToolManager());
		OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("BodyConformToolImportMeshDNACommandChangeUndo", "Body mesh import"));
		// make sure we clear any errors
		OwnerTool->GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserError);
	}
	else
	{
		FText ErrorMessageText;
		if (!GetErrorMessageText(ErrorCode, ErrorMessageText))
		{
			ErrorMessageText = LOCTEXT("FailedToImportBodyMesh", "Failed to import body mesh");
		}

		DisplayConformError(ErrorMessageText);
	}

	{
		UE::MetaHuman::Analytics::FConformEventExtras Extras;
		Extras.Operation = UE::MetaHuman::Analytics::EConformOperation::ImportMesh;
		Extras.Parts = UE::MetaHuman::Analytics::EConformParts::Body;
		Extras.bBodySuccess = bImportMeshSucceeded;
		UE::MetaHuman::Analytics::RecordConformFromDnaEvent(Character, Extras);
	}
}

bool UMetaHumanCharacterImportFromDNAProperties::CanConform() const
{
	return !DNAHead.FilePath.IsEmpty() || !DNABody.FilePath.IsEmpty();
}

void UMetaHumanCharacterImportFromDNAProperties::Conform()
{
	const float ImportWorkProgress = 2.0f;
	FScopedSlowTask ImportTask(ImportWorkProgress, LOCTEXT("ConformDnaTaskMessage", "Conforming from DNA"));
	ImportTask.MakeDialog();

	TNotNull<UMetaHumanCharacterEditorImportTool*> OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorImportTool>();
	TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	// Snapshot state before any mutations so the undo CommandChange captures the correct OldState
	OwnerTool->UpdateOriginalDNABuffer();
	OwnerTool->UpdateOriginalState();
	
	EImportErrorCode BodyErrorCode = EImportErrorCode::Success;
	EImportErrorCode HeadErrorCode = EImportErrorCode::Success;
	bool bConformCompleted = false;
	bool bIsAligningHead = bAlignScale || bAlignRotation || bAlignTranslation;
	
	auto ConformBodyToTarget = [this, &Character, &BodyErrorCode, &bConformCompleted](const TSharedPtr<IDNAReader>& BodyDNAReader, const TSharedPtr<IDNAReader>& HeadDNAReader)
	{
		if (!BodyDNAReader.IsValid())
		{
			BodyErrorCode = EImportErrorCode::InvalidInputData;
			return;
		}

		TArray<FVector3f> TemplateVertices;
		TArray<FVector3f> JointRotations;

		UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
		BodyErrorCode = Subsystem->GetMeshForBodyConforming(Character, BodyDNAReader.ToSharedRef(), HeadDNAReader, TemplateVertices);
		if (BodyErrorCode == EImportErrorCode::Success)
		{
			TArray<FVector3f> JointTranslations;
			BodyErrorCode = Subsystem->GetJointsForBodyConforming(BodyDNAReader.ToSharedRef(), JointTranslations, JointRotations);
		}

		if (BodyErrorCode != EImportErrorCode::Success)
		{
			FText ErrorMessageText;
			if (!GetErrorMessageText(BodyErrorCode, ErrorMessageText))
			{
				ErrorMessageText = LOCTEXT("FailedToConformBodyTemplate", "Failed to conform the body");
			}
			DisplayConformError(ErrorMessageText);
		}
		else
		{
			Subsystem->ConformBody(Character, TemplateVertices, JointRotations, BodyParams.bTargetIsInMetaHumanAPose, BodyParams.bEstimateJointsFromMesh);
			bConformCompleted = true;
		}
	};
	
	TArray<FVector3f> OutVertices;
	TArray<FVector3f> JointRotations;
	TSharedPtr<IDNAReader> BodyDNAReader = ReadDNAFromFile(DNABody.FilePath, EDNADataLayer::All);
	TSharedPtr<IDNAReader> HeadDNAReader = ReadDNAFromFile(DNAHead.FilePath, EDNADataLayer::All);

	bool bConformedFaceStateFromBody = false;
	if (!ImportOptions.bIsolateHeadFromBody)
	{
		if (BodyDNAReader.IsValid() && HeadDNAReader.IsValid() && !bIsAligningHead)
		{
			ConformBodyToTarget(BodyDNAReader, HeadDNAReader);
			if (bConformCompleted)
			{
				Subsystem->FitFaceStateFromBodyWithEyesTeethDNA(Character, HeadDNAReader.ToSharedRef());
				bConformedFaceStateFromBody = true;
			}
		}
		else if (BodyDNAReader.IsValid())
		{
			ConformBodyToTarget(BodyDNAReader, nullptr);
		}
	}

	if (!bConformedFaceStateFromBody && HeadDNAReader.IsValid())
	{
		ImportOptions.bImportWholeRig = false;
		HeadErrorCode = Subsystem->ImportFromFaceDna(Character, HeadDNAReader.ToSharedRef(), ImportOptions);

		if (HeadErrorCode != EImportErrorCode::Success)
		{
			FText ErrorMessageText;
			if (!GetErrorMessageText(HeadErrorCode, ErrorMessageText))
			{
				ErrorMessageText = LOCTEXT("FailedToConformHead", "Failed to conform head mesh");
			}
			DisplayConformError(ErrorMessageText);
		}
		else
		{
			bConformCompleted = true;
		}
	}

	{
		const bool bUserWantsBody = !DNABody.FilePath.IsEmpty();
		const bool bUserWantsHead = !DNAHead.FilePath.IsEmpty();
		if (bUserWantsBody || bUserWantsHead)
		{
			UE::MetaHuman::Analytics::FConformEventExtras Extras;
			Extras.Operation = UE::MetaHuman::Analytics::EConformOperation::Conform;
			Extras.Parts = (bUserWantsBody && bUserWantsHead) ? UE::MetaHuman::Analytics::EConformParts::HeadAndBody
				: bUserWantsHead                                ? UE::MetaHuman::Analytics::EConformParts::Head
				                                                : UE::MetaHuman::Analytics::EConformParts::Body;
			if (bUserWantsBody)
			{
				Extras.bBodySuccess = (BodyErrorCode == EImportErrorCode::Success);
			}
			if (bUserWantsHead)
			{
				Extras.bHeadSuccess = bConformedFaceStateFromBody
					? (BodyErrorCode == EImportErrorCode::Success)
					: (HeadErrorCode == EImportErrorCode::Success);
			}
			UE::MetaHuman::Analytics::RecordConformFromDnaEvent(Character, Extras);
		}
	}

	if (bConformCompleted && (HeadErrorCode == EImportErrorCode::Success || BodyErrorCode == EImportErrorCode::Success))
	{
		TUniquePtr<FImportToolDNACommandChange> CommandChange = MakeUnique<FImportToolDNACommandChange>(
			OwnerTool->GetOriginalFaceDNABuffer(),
			OwnerTool->GetOriginalBodyDNABuffer(),
			OwnerTool->GetOriginalFaceState(),
			OwnerTool->GetOriginalBodyState(),
			Character,
			OwnerTool->GetToolManager());
		OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("ImportFromDNAConformCommandChangeUndo", "Body conform"));
		// make sure we clear any errors
		OwnerTool->GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserError);
	}
}

bool UMetaHumanCharacterImportFromDNAProperties::CanImportJoints() const
{
	return !DNABody.FilePath.IsEmpty() && !ImportOptions.bImportWholeRig;
}

void UMetaHumanCharacterImportFromDNAProperties::ImportJoints()
{
	const float ImportWorkProgress = 2.0f;
	FScopedSlowTask ImportTask(ImportWorkProgress, LOCTEXT("ImportJointsDnaTaskMessage", "Importing Body Joints from DNA"));
	ImportTask.MakeDialog();

	TNotNull < UMetaHumanCharacterEditorImportTool*> OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorImportTool>();
	TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	// Snapshot state before any mutations so the undo CommandChange captures the correct OldState
	OwnerTool->UpdateOriginalState();
	TArray<FVector3f> OutVertices;
	TSharedPtr<IDNAReader> BodyDNAReader = ReadDNAFromFile(DNABody.FilePath, EDNADataLayer::All);
	EImportErrorCode ErrorCode = EImportErrorCode::Success;
	if (!BodyDNAReader.IsValid())
	{
		ErrorCode = EImportErrorCode::InvalidInputData;
	}

	TArray<FVector3f> JointRotations;
	TArray<FVector3f> JointTranslations;
	if (ErrorCode == EImportErrorCode::Success)
	{
		ErrorCode = Subsystem->GetJointsForBodyConforming(BodyDNAReader.ToSharedRef(), JointTranslations, JointRotations);
	}

	if (ErrorCode == EImportErrorCode::Success && Subsystem->SetBodyJoints(Character, JointTranslations, JointRotations, BodyParams.bImportHelperJoints))
	{
		// Add command change to undo stack
		TUniquePtr<FImportToolStateCommandChange> CommandChange = MakeUnique<FImportToolStateCommandChange>(
			OwnerTool->GetOriginalFaceState(),
			OwnerTool->GetOriginalBodyState(),
			Character,
			OwnerTool->GetToolManager());
		OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("BodyConformToolImportJointsDNACommandChangeUndo", "Body Conform bones import"));
		// make sure we clear any errors
		OwnerTool->GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserError);
	}
	else
	{
		FText ErrorMessageText;
		if (!GetErrorMessageText(ErrorCode, ErrorMessageText))
		{
			ErrorMessageText = LOCTEXT("FailedToImportBodyJoints", "Failed to import body bones");
		}
		DisplayConformError(ErrorMessageText);
	}

	{
		UE::MetaHuman::Analytics::FImportJointsEventExtras Extras;
		Extras.Source = TEXT("DNA");
		Extras.bSuccess = (ErrorCode == EImportErrorCode::Success);
		UE::MetaHuman::Analytics::RecordImportJointsEvent(Character, Extras);
	}
}

bool UMetaHumanCharacterImportFromDNAProperties::CanImportWholeRig() const
{
	return !DNABody.FilePath.IsEmpty() || !DNAHead.FilePath.IsEmpty();
}

void UMetaHumanCharacterImportFromDNAProperties::ImportWholeRig()
{
	const float ImportWorkProgress = 2.0f;
	FScopedSlowTask ImportTask(ImportWorkProgress, LOCTEXT("ImportWholeRigDnaTaskMessage", "Importing Whole Rig from DNA"));
	ImportTask.MakeDialog();

	TNotNull<UMetaHumanCharacterEditorImportTool*> OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorImportTool>();
	TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	// Snapshot state before any mutations so the undo CommandChange captures the correct OldState
	OwnerTool->UpdateOriginalDNABuffer();
	OwnerTool->UpdateOriginalState();
	EImportErrorCode ErrorCode = EImportErrorCode::Success;

	TSharedPtr<IDNAReader> BodyDNAReader = ReadDNAFromFile(DNABody.FilePath, EDNADataLayer::All);
	TSharedPtr<IDNAReader> HeadDNAReader = ReadDNAFromFile(DNAHead.FilePath, EDNADataLayer::All);

	if (!DNABody.FilePath.IsEmpty() && !DNAHead.FilePath.IsEmpty())
	{
		if (BodyDNAReader.IsValid() && HeadDNAReader.IsValid())
		{
			ErrorCode = Subsystem->ImportBodyWholeRig(Character, BodyDNAReader.ToSharedRef(), HeadDNAReader.ToSharedRef());
			if (ErrorCode == EImportErrorCode::Success)
			{
				ImportOptions.bImportWholeRig = true;
				ErrorCode = Subsystem->ImportFromFaceDna(Character, HeadDNAReader.ToSharedRef(), ImportOptions);
			}
		}
		else
		{
			ErrorCode = EImportErrorCode::InvalidInputData;
		}
	}
	else if(!DNABody.FilePath.IsEmpty())
	{
		if (BodyDNAReader.IsValid())
		{
			ErrorCode = Subsystem->ImportBodyWholeRig(Character, BodyDNAReader.ToSharedRef(), nullptr);
		}
		else
		{
			ErrorCode = EImportErrorCode::InvalidInputData;
		}
	}
	else if(!DNAHead.FilePath.IsEmpty())
	{
		if (HeadDNAReader.IsValid())
		{
			ImportOptions.bImportWholeRig = true;
			ErrorCode = Subsystem->ImportFromFaceDna(Character, HeadDNAReader.ToSharedRef(), ImportOptions);
		}
		else
		{
			ErrorCode = EImportErrorCode::InvalidInputData;
		}
	}
	else
	{
		ErrorCode = EImportErrorCode::InvalidInputData;
	}

	if (ErrorCode == EImportErrorCode::Success)
	{
		// Add command change to undo stack
		TUniquePtr<FImportToolDNACommandChange> CommandChange = MakeUnique<FImportToolDNACommandChange>(
			OwnerTool->GetOriginalFaceDNABuffer(),
			OwnerTool->GetOriginalBodyDNABuffer(),
			OwnerTool->GetOriginalFaceState(),
			OwnerTool->GetOriginalBodyState(),
			Character,
			OwnerTool->GetToolManager());
		OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("BodyConformToolDNAWholeRigCommandChangeUndo", "Body DNA Import Whole Rig"));


		// make sure we clear any errors
		OwnerTool->GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserError);
	}
	else
	{
		FText ErrorMessageText;
		if (!GetErrorMessageText(ErrorCode, ErrorMessageText))
		{
			ErrorMessageText = LOCTEXT("FailedToImportBodyWholeRig", "Failed to import DNA as whole rig");
		}
		DisplayConformError(ErrorMessageText);
	}

	{
		const bool bUserWantsBody = !DNABody.FilePath.IsEmpty();
		const bool bUserWantsHead = !DNAHead.FilePath.IsEmpty();
		if (bUserWantsBody || bUserWantsHead)
		{
			UE::MetaHuman::Analytics::FConformEventExtras Extras;
			Extras.Operation = UE::MetaHuman::Analytics::EConformOperation::ImportWholeRig;
			Extras.Parts = (bUserWantsBody && bUserWantsHead) ? UE::MetaHuman::Analytics::EConformParts::HeadAndBody
				: bUserWantsHead                                ? UE::MetaHuman::Analytics::EConformParts::Head
				                                                : UE::MetaHuman::Analytics::EConformParts::Body;
			const bool bOverallSucceeded = (ErrorCode == EImportErrorCode::Success);
			if (bUserWantsBody) { Extras.bBodySuccess = bOverallSucceeded; }
			if (bUserWantsHead) { Extras.bHeadSuccess = bOverallSucceeded; }
			UE::MetaHuman::Analytics::RecordConformFromDnaEvent(Character, Extras);
		}
	}
}

bool UMetaHumanCharacterImportFromDNAProperties::GetErrorMessageText(EImportErrorCode InErrorCode, FText& OutErrorMessage) const
{
	switch (InErrorCode)
	{
	case EImportErrorCode::InvalidInputData:
		OutErrorMessage = LOCTEXT("FailedToImportBodyDNAInvalidInputData", "Failed to import DNA: input mesh is not consistent with MetaHuman topology");
		break;
	case EImportErrorCode::InvalidInputBones:
		OutErrorMessage = LOCTEXT("FailedToImportBodyDNAInvalidInputBones", "Failed to import DNA: input mesh bones are not consistent with MetaHuman topology");
		break;
	case EImportErrorCode::InvalidHeadMesh:
		OutErrorMessage = LOCTEXT("FailedToImportBodyDNAInvalidHeadMesh", "Failed to import head DNA: input mesh is not consistent with MetaHuman topology");
		break;
	case EImportErrorCode::CombinedBodyCannotBeImportedAsWholeRig:
		OutErrorMessage = LOCTEXT("FailedToImportBodyDNACombinedAsWholeRig", "Failed to import DNA: can not import combined head and body mesh as body whole rig");
		break;
	default:
		return false;
	}

	return true;
}

bool UMetaHumanCharacterImportFromIdentityProperties::CanImportMesh() const
{
	return !MetaHumanIdentity.IsNull();
}

void UMetaHumanCharacterImportFromIdentityProperties::ImportMesh()
{
	const float ImportWorkProgress = 2.0f;
	FScopedSlowTask ImportIdentityTask(ImportWorkProgress, LOCTEXT("ImportIdentityTaskMessage", "Importing face from Identity asset"));
	ImportIdentityTask.MakeDialog();

	TNotNull<UMetaHumanCharacterEditorImportFromIdentityTool*> OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorImportFromIdentityTool>();
	TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	ImportIdentityTask.EnterProgressFrame(0.5f);
	TNotNull<UMetaHumanIdentity*> ImportedMetaHumanIdentity = MetaHumanIdentity.LoadSynchronous();

	ImportIdentityTask.EnterProgressFrame(1.5f);
	// Snapshot state before any mutations so the undo CommandChange captures the correct OldState
	OwnerTool->UpdateOriginalState();
	EImportErrorCode ErrorCode = UMetaHumanCharacterEditorSubsystem::Get()->ImportFromIdentity(Character, ImportedMetaHumanIdentity, ImportOptions);

	// Analytics: Identity tool's Import Mesh button is head-only (Identity is a face-conform asset).
	{
		UE::MetaHuman::Analytics::FConformEventExtras Extras;
		Extras.Operation = UE::MetaHuman::Analytics::EConformOperation::ImportMesh;
		Extras.Parts = UE::MetaHuman::Analytics::EConformParts::Head;
		Extras.bHeadSuccess = (ErrorCode == EImportErrorCode::Success);
		UE::MetaHuman::Analytics::RecordConformFromIdentityEvent(Character, Extras);
	}

	if (ErrorCode == EImportErrorCode::Success)
	{
		// Add command change to undo stack
		TUniquePtr<FImportToolStateCommandChange> CommandChange = MakeUnique<FImportToolStateCommandChange>(
			OwnerTool->GetOriginalFaceState(),
			OwnerTool->GetOriginalBodyState(),
			Character,
			OwnerTool->GetToolManager());

		OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("ConformToolIdentityCommandChangeUndo", "Conform Tool Identity Import"));
		// make sure we clear any errors
		OwnerTool->GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserError);

	}
	else
	{
		FText ErrorMessageText;
		switch (ErrorCode)
		{
		case EImportErrorCode::FittingError:
			ErrorMessageText = LOCTEXT("FailedToImportIdentityFittingError", "Failed to import Identity: fitting error");
			break;
		case EImportErrorCode::NoHeadMeshPresent:
			ErrorMessageText = LOCTEXT("FailedToImportIdentityNoHeadMeshPresentError", "Failed to import Identity: no conformed head mesh present");
			break;
		case EImportErrorCode::NoEyeMeshesPresent:
			ErrorMessageText = LOCTEXT("FailedToImportIdentityNoEyeMeshesPresentError", "Failed to import Identity: no conformed eye meshes present");
			break;
		case EImportErrorCode::NoTeethMeshPresent:
			ErrorMessageText = LOCTEXT("FailedToImportIdentityNoTeethMeshPresentError", "Failed to import Identity: no conformed teeth mesh present");
			break;
		case EImportErrorCode::IdentityNotConformed:
			ErrorMessageText = LOCTEXT("FailedToImportIdentityIdentityNotConformedError", "Failed to import Identity: Identity asset has not been conformed");
			break;
		default:
			// just give a general error message
			ErrorMessageText = LOCTEXT("FailedToImportIdentityGeneral", "Failed to import Identity");
			break;
		}

		DisplayConformError(ErrorMessageText);
	}

}

void UMetaHumanCharacterImportFromTemplateProperties::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromTemplateProperties, bAlignScale) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromTemplateProperties, bAlignRotation) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromTemplateProperties, bAlignTranslation))
	{
		EAlignmentOptions AlignmentOptions = EAlignmentOptions::None;
		if (bAlignScale && bAlignRotation && bAlignTranslation)
		{
			AlignmentOptions = EAlignmentOptions::ScalingRotationTranslation;
		}
		else if (bAlignRotation && bAlignTranslation)
		{
			AlignmentOptions = EAlignmentOptions::RotationTranslation;
		}
		else if (bAlignScale && bAlignTranslation)
		{
			AlignmentOptions = EAlignmentOptions::ScalingTranslation;
		}
		else if (bAlignTranslation)
		{
			AlignmentOptions = EAlignmentOptions::Translation;
		}

		ImportOptions.AlignmentOptions = AlignmentOptions;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromTemplateProperties, bMatchVerticesByUVs))
	{
		ImportOptions.bMatchVerticesByUVs = bMatchVerticesByUVs;
	}
}

bool UMetaHumanCharacterImportFromTemplateProperties::CanImportMesh() const
{
	const bool bCanImport = BodyMesh.ToSoftObjectPath().IsValid();
	return bCanImport;
}

void UMetaHumanCharacterImportFromTemplateProperties::ImportMesh()
{
	const float ImportWorkProgress = 2.0f;
	FScopedSlowTask ImportTask(ImportWorkProgress, LOCTEXT("ImportTemplateTaskMessage", "Import Mesh from Template"));
	ImportTask.MakeDialog();

	TNotNull<UMetaHumanCharacterEditorImportTool*> OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorImportTool>();
	TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	ImportTask.EnterProgressFrame(0.5f);
	
	UObject* ImportedMetaHumanBodyTemplate = BodyMesh.ToSoftObjectPath().IsValid() ? BodyMesh.LoadSynchronous() : nullptr;
	UObject* ImportedMetaHumanHeadTemplate = HeadMesh.ToSoftObjectPath().IsValid() ? HeadMesh.LoadSynchronous() : nullptr;
	
	UObject* ImportedLeftEyeMesh = nullptr;
	UObject* ImportedRightEyeMesh = nullptr;
	UObject* ImportedTeethMesh = nullptr;
	if (Cast<UStaticMesh>(ImportedMetaHumanHeadTemplate))
	{
		if (!LeftEyeMesh.IsNull())
		{
			ImportedLeftEyeMesh = LeftEyeMesh.LoadSynchronous();
		}
		if (!RightEyeMesh.IsNull())
		{
			ImportedRightEyeMesh = RightEyeMesh.LoadSynchronous();
		}
		if (!TeethMesh.IsNull())
		{
			ImportedTeethMesh = TeethMesh.LoadSynchronous();
		}
	}

	ImportTask.EnterProgressFrame(1.5f);
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	// Snapshot state before any mutations so the undo CommandChange captures the correct OldState
	OwnerTool->UpdateOriginalState();
	TArray<FVector3f> OutVertices;

	EImportErrorCode BodyImportErrorCode = EImportErrorCode::GeneralError;
	if (ImportedMetaHumanBodyTemplate)
	{
		BodyImportErrorCode = Subsystem->GetMeshForBodyConforming(Character, ImportedMetaHumanBodyTemplate, ImportedMetaHumanHeadTemplate, bMatchVerticesByUVs, OutVertices);
	}

	bool bConformBody = false;
	if (ImportedMetaHumanBodyTemplate && BodyImportErrorCode == EImportErrorCode::Success)
	{
		bConformBody = Subsystem->SetBodyMesh(Character, OutVertices, BodyParams.bAutoRigHelperJoints);
	}

	{
		const bool bUserWantsBody = BodyMesh.ToSoftObjectPath().IsValid();
		const bool bUserWantsHead = HeadMesh.ToSoftObjectPath().IsValid();
		if (bUserWantsBody || bUserWantsHead)
		{
			UE::MetaHuman::Analytics::FConformEventExtras Extras;
			Extras.Operation = UE::MetaHuman::Analytics::EConformOperation::ImportMesh;
			Extras.Parts = (bUserWantsBody && bUserWantsHead) ? UE::MetaHuman::Analytics::EConformParts::HeadAndBody
				: bUserWantsHead                                ? UE::MetaHuman::Analytics::EConformParts::Head
				                                                : UE::MetaHuman::Analytics::EConformParts::Body;
			if (bUserWantsBody)
			{
				Extras.bBodySuccess = (ImportedMetaHumanBodyTemplate != nullptr)
					&& (BodyImportErrorCode == EImportErrorCode::Success)
					&& bConformBody;
			}
			if (bUserWantsHead)
			{
				Extras.bHeadSuccess = (ImportedMetaHumanHeadTemplate != nullptr);
				Extras.bImportEyes = (ImportedLeftEyeMesh != nullptr) || (ImportedRightEyeMesh != nullptr);
				Extras.bImportTeeth = (ImportedTeethMesh != nullptr);
			}
			UE::MetaHuman::Analytics::RecordConformFromTemplateEvent(Character, Extras);
		}
	}

	if (BodyImportErrorCode == EImportErrorCode::Success && bConformBody)
	{
		// Add command change to undo stack
		TUniquePtr<FImportToolStateCommandChange> CommandChange = MakeUnique<FImportToolStateCommandChange>(
			OwnerTool->GetOriginalFaceState(),
			OwnerTool->GetOriginalBodyState(),
			Character,
			OwnerTool->GetToolManager());
		
		OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("ImportMeshTemplateCommandChangeUndo", "Mesh Import"));
		
		// make sure we clear any errors
		OwnerTool->GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserError);
	}
	else
	{
		if(BodyImportErrorCode != EImportErrorCode::Success)
		{
			FText BodyErrorMessageText;
			if (!GetErrorMessageText(BodyImportErrorCode, BodyErrorMessageText))
			{
				BodyErrorMessageText = LOCTEXT("FailedToImportBodyMeshFromTemplate", "Failed to import body mesh");
			}
			DisplayConformError(BodyErrorMessageText);
		}
	}
}

bool UMetaHumanCharacterImportFromTemplateProperties::CanConform() const
{
	const bool bCanConform =
		BodyMesh.ToSoftObjectPath().IsValid() ||
		HeadMesh.ToSoftObjectPath().IsValid();

	return bCanConform;
}

void UMetaHumanCharacterImportFromTemplateProperties::Conform()
{
	const float ImportWorkProgress = 2.0f;
	FScopedSlowTask ImportTask(ImportWorkProgress, LOCTEXT("ConformTemplateTaskMessage", "Conforming from Template"));
	ImportTask.MakeDialog();

	TNotNull<UMetaHumanCharacterEditorImportTool*> OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorImportTool>();
	TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	// Snapshot state before any mutations so the undo CommandChange captures the correct OldState
	OwnerTool->UpdateOriginalState();
	TArray<FVector3f> OutVertices;
	TArray<FVector3f> JointRotations;

	UObject* ImportedMetaHumanBodyTemplate = BodyMesh.ToSoftObjectPath().IsValid() ? BodyMesh.LoadSynchronous() : nullptr;
	UObject* ImportedMetaHumanHeadTemplate = HeadMesh.ToSoftObjectPath().IsValid() ? HeadMesh.LoadSynchronous() : nullptr;

	UObject* ImportedLeftEyeMesh = nullptr;
	UObject* ImportedRightEyeMesh = nullptr;
	UObject* ImportedTeethMesh = nullptr;
	if (Cast<UStaticMesh>(ImportedMetaHumanHeadTemplate))
	{
		if (!LeftEyeMesh.IsNull())
		{
			ImportedLeftEyeMesh = LeftEyeMesh.LoadSynchronous();
		}
		if (!RightEyeMesh.IsNull())
		{
			ImportedRightEyeMesh = RightEyeMesh.LoadSynchronous();
		}
		if (!TeethMesh.IsNull())
		{
			ImportedTeethMesh = TeethMesh.LoadSynchronous();
		}
	}

	bool bConformCompleted = false;
	bool bIsAligningHead = bAlignScale || bAlignRotation || bAlignTranslation;
	EImportErrorCode BodyErrorCode = EImportErrorCode::Success;
	EImportErrorCode HeadErrorCode = EImportErrorCode::Success;
	
	auto ConformBodyToTarget = [this, &Character, &BodyErrorCode, &bConformCompleted](UObject* BodyTemplate, UObject* HeadTemplate)
	{
		TArray<FVector3f> TemplateVertices;
		TArray<FVector3f> JointRotations;

		UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
		BodyErrorCode = Subsystem->GetMeshForBodyConforming(Character, BodyTemplate, HeadTemplate, bMatchVerticesByUVs, TemplateVertices);
		if (BodyErrorCode == EImportErrorCode::Success)
		{
			USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(BodyTemplate);
			if (SkeletalMesh)
			{
				TArray<FVector3f> JointTranslations;
				BodyErrorCode = Subsystem->GetJointsForBodyConforming(SkeletalMesh, JointTranslations, JointRotations);
			}
		}

		if (BodyErrorCode != EImportErrorCode::Success)
		{
			FText ErrorMessageText;
			if (!GetErrorMessageText(BodyErrorCode, ErrorMessageText))
			{
				ErrorMessageText = LOCTEXT("FailedToConformBodyTemplate", "Failed to conform the body");
			}
			DisplayConformError(ErrorMessageText);
		}
		else
		{
			Subsystem->ConformBody(Character, TemplateVertices, JointRotations, BodyParams.bTargetIsInMetaHumanAPose, BodyParams.bEstimateJointsFromMesh);
			bConformCompleted = true;
		}
	};
	
	bool bConformedFaceStateFromBody = false;
	if (!ImportOptions.bIsolateHeadFromBody)
	{
		if (ImportedMetaHumanBodyTemplate && ImportedMetaHumanHeadTemplate && !bIsAligningHead && !BodyParams.bTargetIsInMetaHumanAPose)
		{
			ConformBodyToTarget(ImportedMetaHumanBodyTemplate, ImportedMetaHumanHeadTemplate);
			if (bConformCompleted)
			{
				Subsystem->FitFaceStateFromBodyWithEyesTeethTemplate(Character, ImportedTeethMesh, ImportedLeftEyeMesh, ImportedRightEyeMesh, bMatchVerticesByUVs);
				bConformedFaceStateFromBody = true;
			}
		}
		else if (ImportedMetaHumanBodyTemplate && ImportedMetaHumanHeadTemplate && !bIsAligningHead)
		{
			ConformBodyToTarget(ImportedMetaHumanBodyTemplate, ImportedMetaHumanHeadTemplate);
		}
		else if (ImportedMetaHumanBodyTemplate)
		{
			ConformBodyToTarget(ImportedMetaHumanBodyTemplate, nullptr);
		}
	}
	
	if (!bConformedFaceStateFromBody && ImportedMetaHumanHeadTemplate)
	{
		HeadErrorCode = Subsystem->ImportFromTemplate(Character, ImportedMetaHumanHeadTemplate, ImportedLeftEyeMesh, ImportedRightEyeMesh, ImportedTeethMesh, ImportOptions);

		if (HeadErrorCode != EImportErrorCode::Success)
		{
			FText ErrorMessageText;
			if (!GetErrorMessageText(HeadErrorCode, ErrorMessageText))
			{
				ErrorMessageText = LOCTEXT("FailedToConformHeadTemplate", "Failed to conform the head");
			}
			DisplayConformError(ErrorMessageText);
		}
		else
		{
			bConformCompleted = true;
		}
	}

	{
		const bool bUserWantsBody = BodyMesh.ToSoftObjectPath().IsValid();
		const bool bUserWantsHead = HeadMesh.ToSoftObjectPath().IsValid();
		if (bUserWantsBody || bUserWantsHead)
		{
			UE::MetaHuman::Analytics::FConformEventExtras Extras;
			Extras.Operation = UE::MetaHuman::Analytics::EConformOperation::Conform;
			Extras.Parts = (bUserWantsBody && bUserWantsHead) ? UE::MetaHuman::Analytics::EConformParts::HeadAndBody
				: bUserWantsHead                                ? UE::MetaHuman::Analytics::EConformParts::Head
				                                                : UE::MetaHuman::Analytics::EConformParts::Body;
			if (bUserWantsBody)
			{
				Extras.bBodySuccess = (ImportedMetaHumanBodyTemplate != nullptr)
					&& (BodyErrorCode == EImportErrorCode::Success);
			}
			if (bUserWantsHead)
			{
				Extras.bHeadSuccess = (ImportedMetaHumanHeadTemplate != nullptr)
					&& (HeadErrorCode == EImportErrorCode::Success);
				Extras.bImportEyes = (ImportedLeftEyeMesh != nullptr) || (ImportedRightEyeMesh != nullptr);
				Extras.bImportTeeth = (ImportedTeethMesh != nullptr);
			}
			UE::MetaHuman::Analytics::RecordConformFromTemplateEvent(Character, Extras);
		}
	}

	if (bConformCompleted && (HeadErrorCode == EImportErrorCode::Success || BodyErrorCode == EImportErrorCode::Success))
	{
		TUniquePtr<FImportToolStateCommandChange> CommandChange = MakeUnique<FImportToolStateCommandChange>(
			OwnerTool->GetOriginalFaceState(),
			OwnerTool->GetOriginalBodyState(),
			Character,
			OwnerTool->GetToolManager());
		OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("ImportFromTemplateConformCommandChangeUndo", "Body conform"));
		// make sure we clear any errors
		OwnerTool->GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserError);
	}
}

bool UMetaHumanCharacterImportFromTemplateProperties::CanImportJoints() const
{
	if (!BodyMesh.ToSoftObjectPath().IsValid())
	{
		return false;
	}
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(BodyMesh.ToSoftObjectPath());
	if (!AssetData.IsValid())
	{
		return false;
	}
	return AssetData.AssetClassPath == USkeletalMesh::StaticClass()->GetClassPathName();
}

void UMetaHumanCharacterImportFromTemplateProperties::ImportJoints()
{
	const float ImportWorkProgress = 2.0f;
	FScopedSlowTask ImportTask(ImportWorkProgress, LOCTEXT("ImportJointsTemplateTaskMessage", "Import Body Joints from Template"));
	ImportTask.MakeDialog();

	TNotNull<UMetaHumanCharacterEditorImportTool*> OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorImportTool>();
	TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	TArray<FVector3f> JointTranslations;
	TArray<FVector3f> JointRotations;
	USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(BodyMesh.LoadSynchronous());

	EImportErrorCode ErrorCode = EImportErrorCode::InvalidInputData;
	if (SkelMesh != nullptr)
	{
		ErrorCode = Subsystem->GetJointsForBodyConforming(SkelMesh, JointTranslations, JointRotations);

		if (ErrorCode == EImportErrorCode::Success && Subsystem->SetBodyJoints(Character, JointTranslations, JointRotations, BodyParams.bImportHelperJoints))
		{
			// Add command change to undo stack
			TUniquePtr<FImportToolStateCommandChange> CommandChange = MakeUnique<FImportToolStateCommandChange>(
				OwnerTool->GetOriginalFaceState(),
				OwnerTool->GetOriginalBodyState(),
				Character,
				OwnerTool->GetToolManager());

			// update original state in tool so undo works as expected
			OwnerTool->UpdateOriginalState();
			OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("BodyConformToolImportJointsTemplateCommandChangeUndo", "Body bones import"));

			// make sure we clear any errors
			OwnerTool->GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserError);
		}
		else
		{
			FText ErrorMessageText;
			if (!GetErrorMessageText(ErrorCode, ErrorMessageText))
			{
				ErrorMessageText = LOCTEXT("FailedToImportBodyJointsFromTemplate", "Failed to import body joints");
			}

			DisplayConformError(ErrorMessageText);
		}
	}

	{
		UE::MetaHuman::Analytics::FImportJointsEventExtras Extras;
		Extras.Source = TEXT("Template");
		Extras.bSuccess = (ErrorCode == EImportErrorCode::Success);
		UE::MetaHuman::Analytics::RecordImportJointsEvent(Character, Extras);
	}
}

bool UMetaHumanCharacterImportFromTemplateProperties::GetErrorMessageText(EImportErrorCode InErrorCode, FText& OutErrorMessage) const
{
	switch (InErrorCode)
	{
	case EImportErrorCode::FittingError:
		OutErrorMessage = LOCTEXT("FailedToImportFromTemplateFittingError", "Failed to import Template Mesh: failed to fit to mesh.");
		break;
	case EImportErrorCode::InvalidInputData:
		OutErrorMessage = LOCTEXT("FailedToImportBodyTemplateInvalidInputData", "Failed to import Template Mesh: input mesh is not consistent with MetaHuman topology");
		break;
	case EImportErrorCode::InvalidInputBones:
		OutErrorMessage = LOCTEXT("FailedToImportBodyTemplateInvalidInputBones", "Failed to import Template Mesh: input mesh bones are not consistent with MetaHuman topology");
		break;
	case EImportErrorCode::InvalidHeadMesh:
		OutErrorMessage = LOCTEXT("FailedToImportBodyTemplateInvalidHeadMesh", "Failed to import head Template Mesh: input mesh is not consistent with MetaHuman topology");
		break;
	case EImportErrorCode::InvalidLeftEyeMesh:
		OutErrorMessage = LOCTEXT("FailedToImportFromTemplateInvalidLeftEyeMeshError", "Failed to import Template Mesh: input left eye mesh is not consistent with MetaHuman topology");
		break;
	case EImportErrorCode::InvalidRightEyeMesh:
		OutErrorMessage = LOCTEXT("FailedToImportFromTemplateInvalidRightEyeMeshError", "Failed to import Template Mesh: input right eye mesh is not consistent with MetaHuman topology");
		break;
	case EImportErrorCode::InvalidTeethMesh:
		OutErrorMessage = LOCTEXT("FailedToImportFromTemplateInvalidTeethMeshError", "Failed to import Template Mesh: input teeth mesh is not consistent with MetaHuman topology");
		break;
	default:
		// just give a general error message
		OutErrorMessage = LOCTEXT("FailedToImportFromTemplateGeneral", "Failed to import Template Mesh");
		break;
	}

	return true;
}

void UMetaHumanCharacterEditorImportTool::Shutdown(EToolShutdownType InShutdownType)
{
	Super::Shutdown(InShutdownType);

	ImportProperties->SaveProperties(this);
}

void UMetaHumanCharacterEditorImportTool::UpdateOriginalState()
{
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	check(MetaHumanCharacter);
	const UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	OriginalFaceState = Subsystem->CopyFaceState(MetaHumanCharacter);
	OriginalBodyState = Subsystem->CopyBodyState(MetaHumanCharacter);
}

void UMetaHumanCharacterEditorImportTool::UpdateOriginalDNABuffer()
{
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	check(MetaHumanCharacter);
	const UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	OriginalFaceState = Subsystem->CopyFaceState(MetaHumanCharacter);
	OriginalFaceDNABuffer = MetaHumanCharacter->GetFaceDNABuffer();
	OriginalBodyState = Subsystem->CopyBodyState(MetaHumanCharacter);
	OriginalBodyDNABuffer = MetaHumanCharacter->GetBodyDNABuffer();
}

TSharedRef<const FMetaHumanCharacterIdentity::FState> UMetaHumanCharacterEditorImportTool::GetOriginalFaceState() const
{
	return OriginalFaceState.ToSharedRef();
}

TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> UMetaHumanCharacterEditorImportTool::GetOriginalBodyState() const
{
	return OriginalBodyState.ToSharedRef();
}

const TArray<uint8>& UMetaHumanCharacterEditorImportTool::GetOriginalFaceDNABuffer() const
{
	return OriginalFaceDNABuffer;
}

const TArray<uint8>& UMetaHumanCharacterEditorImportTool::GetOriginalBodyDNABuffer() const
{
	return OriginalBodyDNABuffer;
}

void UMetaHumanCharacterEditorImportFromDNATool::Setup()
{
	Super::Setup();

	SetToolDisplayName(LOCTEXT("ImportFromDNAToolName", "Import From DNA"));

	// Save the original state to restored in case the tool is cancelled
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	check(MetaHumanCharacter);
	const UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	OriginalFaceState = Subsystem->CopyFaceState(MetaHumanCharacter);
	OriginalFaceDNABuffer = MetaHumanCharacter->GetFaceDNABuffer();
	OriginalBodyState = Subsystem->CopyBodyState(MetaHumanCharacter);
	OriginalBodyDNABuffer = MetaHumanCharacter->GetBodyDNABuffer();

	ImportProperties = NewObject<UMetaHumanCharacterImportFromDNAProperties>(this);
	ImportProperties->RestoreProperties(this);
}

void UMetaHumanCharacterEditorImportFromIdentityTool::Setup()
{
	Super::Setup();

	SetToolDisplayName(LOCTEXT("ImportFormIdentityToolName", "Import Form Identity"));

	// Save the original state to restored in case the tool is cancelled
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	check(MetaHumanCharacter);
	const UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	OriginalFaceState = Subsystem->CopyFaceState(MetaHumanCharacter);
	OriginalFaceDNABuffer = MetaHumanCharacter->GetFaceDNABuffer();
	OriginalBodyState = Subsystem->CopyBodyState(MetaHumanCharacter);
	OriginalBodyDNABuffer = MetaHumanCharacter->GetBodyDNABuffer();

	ImportProperties = NewObject<UMetaHumanCharacterImportFromIdentityProperties>(this);
	ImportProperties->RestoreProperties(this);
}

void UMetaHumanCharacterEditorImportFromTemplateTool::Setup()
{
	Super::Setup();

	SetToolDisplayName(LOCTEXT("ImportFromTemplateToolName", "Import From Template"));

	// Save the original state to restored in case the tool is cancelled
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	check(MetaHumanCharacter);
	const UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	OriginalFaceState = Subsystem->CopyFaceState(MetaHumanCharacter);
	OriginalFaceDNABuffer = MetaHumanCharacter->GetFaceDNABuffer();
	OriginalBodyState = Subsystem->CopyBodyState(MetaHumanCharacter);
	OriginalBodyDNABuffer = MetaHumanCharacter->GetBodyDNABuffer();

	ImportProperties = NewObject<UMetaHumanCharacterImportFromTemplateProperties>(this);
	ImportProperties->RestoreProperties(this);
}

#undef LOCTEXT_NAMESPACE