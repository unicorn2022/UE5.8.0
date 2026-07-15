// Copyright Epic Games, Inc. All Rights Reserved.

#include "PersistSandboxWorkflow.h"

#include "PersistWorkflowUserChoice.h"
#include "Algo/Transform.h"
#include "Types/GatheredFileChanges.h"

namespace UE::SandboxedEditing
{
FPersistSandboxWorkflow::FPersistSandboxWorkflow(
	const TSharedRef<FSandboxSystemModel>& InModel, FileSandboxCore::FGatheredFileChanges InChanges,
	FCleanupWorkflow InOnWorkflowEnded
	)
	: Model(InModel)
	, OnCleanupWorkflowDelegate(MoveTemp(InOnWorkflowEnded))
	, PersistableFiles(InChanges)
{}

void FPersistSandboxWorkflow::ConfirmPersist(TArray<FString> InFilesToPersist) const
{
	EndWorkflow(InFilesToPersist.IsEmpty() 
		? FPersistWorkflowUserChoice::MakePersistNone()
		: FPersistWorkflowUserChoice::MakePersist(MoveTemp(InFilesToPersist))
		);
}

void FPersistSandboxWorkflow::CancelPersist() const
{
	EndWorkflow(
		FPersistWorkflowUserChoice::MakeCancelled()
		);
}

void FPersistSandboxWorkflow::EndWorkflow(const FPersistWorkflowUserChoice& InResult) const
{
	OnWorkflowEndedDelegate.Broadcast();
	OnCleanupWorkflowDelegate.ExecuteIfBound(InResult);
}
}
