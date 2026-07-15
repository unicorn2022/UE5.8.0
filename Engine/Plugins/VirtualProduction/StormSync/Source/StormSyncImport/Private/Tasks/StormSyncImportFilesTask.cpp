// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/StormSyncImportFilesTask.h"

#include "StormSyncCoreDelegates.h"
#include "StormSyncImportLog.h"
#include "Subsystems/StormSyncImportSubsystem.h"

void FStormSyncImportFilesTask::Run()
{
	UE_LOGF(LogStormSyncImport, Display, "FStormSyncImportFilesTask::Run for %ls", *Filename);
	UStormSyncImportSubsystem::Get().PerformFileImport(Filename);
	FStormSyncCoreDelegates::OnFileImported.Broadcast(Filename);
}
