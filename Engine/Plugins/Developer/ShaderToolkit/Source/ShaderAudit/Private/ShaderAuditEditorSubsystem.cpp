// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderAuditEditorSubsystem.h"

void UShaderAuditEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	UEditorSubsystem::Initialize(Collection);
}

void UShaderAuditEditorSubsystem::Deinitialize()
{
}

const TArray<TSharedPtr<FShaderAuditSession>>& UShaderAuditEditorSubsystem::GetSessions() const
{
	return FShaderAuditSession::GetSessions();
}

FOnShaderSHKFileLoaded& UShaderAuditEditorSubsystem::OnSessionLoaded()
{
	return OnSessionLoadedDelegate;
}
