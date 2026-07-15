// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveToolChange.h"
#include "InteractiveToolManager.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterBodyIdentity.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterIdentity.h"

/**
 * Tool Command change for undo/redo transactions.
 */
class FMetaHumanCharacterEditorToolCommandChange : public FToolCommandChange
{
public:
	FMetaHumanCharacterEditorToolCommandChange(TNotNull<UInteractiveToolManager*> InToolManager)
		: ToolManager{ InToolManager }
	{
	}

	//~Begin FToolCommandChange interface
	virtual bool HasExpired(UObject* InObject) const override
	{
		// If the ToolManager is not valid anymore it means the asset editor was closed so mark the transaction as expired
		return !ToolManager.IsValid();
	}
	//~End FToolCommandChange interface

protected:
	TWeakObjectPtr<UInteractiveToolManager> ToolManager;
};


DECLARE_DELEGATE_TwoParams(FOnSettingsUpdateDelegate, TWeakObjectPtr<UInteractiveToolManager>, const FMetaHumanCharacterFaceEvaluationSettings&);

/**
 * Base class for MetaHuman Character command change for undo/redo transactions.
 */
class FMetaHumanCharacterEditorFaceEvaluationSettingsCommandChange : public FMetaHumanCharacterEditorToolCommandChange
{
public:
	FMetaHumanCharacterEditorFaceEvaluationSettingsCommandChange(
		TNotNull<UMetaHumanCharacter*> InCharacter,
		const FMetaHumanCharacterFaceEvaluationSettings& InOldSettings,
		FOnSettingsUpdateDelegate InOnSettingsUpdateDelegate,
		TNotNull<UInteractiveToolManager*> InToolManager)
		: FMetaHumanCharacterEditorToolCommandChange(InToolManager)
		, OldSettings{ InOldSettings }
		, NewSettings{ InCharacter->FaceEvaluationSettings }
		, OnSettingsUpdateDelegate{ InOnSettingsUpdateDelegate }
	{
	}

	//~Begin FCommandChange interface
	virtual void Apply(UObject* InObject) override;
	virtual void Revert(UObject* InObject) override;
	//~End FCommandChange interface

private:
	FMetaHumanCharacterFaceEvaluationSettings OldSettings;
	FMetaHumanCharacterFaceEvaluationSettings NewSettings;

	FOnSettingsUpdateDelegate OnSettingsUpdateDelegate;
};

/**
 * Body Tool Command change for undo/redo transactions.
 */
class FMetaHumanCharacterEditorBodyToolCommandChange : public FToolCommandChange
{
public:
	FMetaHumanCharacterEditorBodyToolCommandChange(
		TSharedRef<FMetaHumanCharacterBodyIdentity::FState> InOldState,
		TSharedRef<FMetaHumanCharacterBodyIdentity::FState> InNewState,
		TNotNull<UInteractiveToolManager*> InToolManager)
		: OldState(InOldState)
		, NewState(InNewState)
		, ToolManager(InToolManager)
	{
	}

	virtual void Apply(UObject* Object) override
	{
		UMetaHumanCharacter* Character = CastChecked<UMetaHumanCharacter>(Object);
		UMetaHumanCharacterEditorSubsystem::Get()->CommitBodyState(Character, NewState);
	}

	virtual void Revert(UObject* Object) override
	{
		UMetaHumanCharacter* Character = CastChecked<UMetaHumanCharacter>(Object);
		UMetaHumanCharacterEditorSubsystem::Get()->CommitBodyState(Character, OldState);
	}

	virtual bool HasExpired(UObject* InObject) const override
	{
		return !ToolManager.IsValid();
	}

private:
	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> OldState;
	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> NewState;

	TWeakObjectPtr<UInteractiveToolManager> ToolManager;
};

/**
 * Combined body and face command change for undo/redo transactions. Used when both body and face
 * states change together as a single undoable operation (e.g. mesh conform, blend both types).
 */
class FMetaHumanCharacterEditorStateCommandChange : public FMetaHumanCharacterEditorToolCommandChange
{
public:
	FMetaHumanCharacterEditorStateCommandChange(
		TNotNull<UInteractiveToolManager*> InToolManager,
		TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InOldBodyState,
		TSharedRef<const FMetaHumanCharacterIdentity::FState> InOldFaceState,
		TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InNewBodyState,
		TSharedRef<const FMetaHumanCharacterIdentity::FState> InNewFaceState,
		FString InDescription)
		: FMetaHumanCharacterEditorToolCommandChange(InToolManager)
		, OldBodyState(InOldBodyState)
		, OldFaceState(InOldFaceState)
		, NewBodyState(InNewBodyState)
		, NewFaceState(InNewFaceState)
		, Description(InDescription)
	{}

	virtual void Apply(UObject* InObject) override
	{
		ApplyState(InObject, NewBodyState, NewFaceState);
	}

	virtual void Revert(UObject* InObject) override
	{
		ApplyState(InObject, OldBodyState, OldFaceState);
	}

	virtual FString ToString() const override { return Description; }

private:
	void ApplyState(UObject* InObject,
		TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InBodyState,
		TSharedRef<const FMetaHumanCharacterIdentity::FState> InFaceState)
	{
		UMetaHumanCharacter* Character = CastChecked<UMetaHumanCharacter>(InObject);
		UMetaHumanCharacterEditorSubsystem::Get()->CommitFaceState(Character, InFaceState);
		UMetaHumanCharacterEditorSubsystem::Get()->CommitBodyState(Character, InBodyState, UMetaHumanCharacterEditorSubsystem::EBodyMeshUpdateMode::Minimal);
	}

	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> OldBodyState;
	TSharedRef<const FMetaHumanCharacterIdentity::FState> OldFaceState;
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> NewBodyState;
	TSharedRef<const FMetaHumanCharacterIdentity::FState> NewFaceState;
	FString Description;
};
