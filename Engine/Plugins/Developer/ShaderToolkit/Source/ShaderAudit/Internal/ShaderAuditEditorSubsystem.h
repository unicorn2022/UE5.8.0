// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"
#include "ShaderAuditSession.h"

#include "ShaderAuditEditorSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnShaderSHKFileLoaded)

UCLASS(MinimalAPI)
class UShaderAuditEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	/** UEditorSubsystem */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	const TArray<TSharedPtr<FShaderAuditSession>>& GetSessions() const;
	FOnShaderSHKFileLoaded& OnSessionLoaded();

private:
	FOnShaderSHKFileLoaded OnSessionLoadedDelegate;
};
