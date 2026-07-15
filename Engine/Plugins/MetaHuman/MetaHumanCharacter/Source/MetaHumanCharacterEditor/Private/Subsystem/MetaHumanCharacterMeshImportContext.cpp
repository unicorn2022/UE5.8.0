// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subsystem/MetaHumanCharacterMeshImportContext.h"

void FMetaHumanCharacterEditorMeshImportContext::MeshImportFinished()
{
	MeshImportTaskRunner.Reset();
	MeshImportStartTime = 0.0f;
	MeshImportProgressHandle.Reset();
	MeshImportNotificationItem.Reset();
}

bool FMetaHumanCharacterEditorMeshImportContext::HasActiveRequest() const
{
	return MeshImportTaskRunner.IsValid();
}
