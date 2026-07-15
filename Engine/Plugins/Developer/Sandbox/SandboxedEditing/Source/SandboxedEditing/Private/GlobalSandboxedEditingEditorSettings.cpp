// Copyright Epic Games, Inc. All Rights Reserved.

#include "GlobalSandboxedEditingEditorSettings.h"

#include "Misc/Paths.h"
#include "Misc/PathViews.h"

namespace UE::SandboxedEditing::GlobalSettingsDetail
{
static FString GetDirectoryPath(const FString& InPath)
{
	FString NormalizedPath = InPath;
	FPaths::NormalizeDirectoryName(NormalizedPath);
	const bool bIsDirectoryPath = FPaths::GetExtension(NormalizedPath).IsEmpty();
	return bIsDirectoryPath
		? NormalizedPath
		: FPaths::GetPath(NormalizedPath);
}
}

void UGlobalSandboxedEditingEditorSettings::SetDefaultExportDirectory(const FString& InPath)
{
	FString DirectoryPath = UE::SandboxedEditing::GlobalSettingsDetail::GetDirectoryPath(InPath);
	if (DirectoryPath != DefaultExportDirectory)
	{
		DefaultExportDirectory = MoveTemp(DirectoryPath);
		SaveConfig();
	}
}

void UGlobalSandboxedEditingEditorSettings::SetDefaultImportDirectory(const FString& InPath)
{
	FString DirectoryPath = UE::SandboxedEditing::GlobalSettingsDetail::GetDirectoryPath(InPath);
	if (DirectoryPath != DefaultImportDirectory)
	{
		DefaultImportDirectory = MoveTemp(DirectoryPath);
		SaveConfig();
	}
}
