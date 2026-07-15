// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tasks/Task.h"

class ISTSourceControlService;
class FModelInterface;

/**
 * Run the SubmitTool .
 */
int RunSubmitTool(const TCHAR* Commandline);
UE::Tasks::TTask<TUniquePtr<FModelInterface>> SyncConfigAndInitializeModelInterface();
FString GetUserPrefsPath();
