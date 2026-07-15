// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Notifications/NotificationManager.h"
#include "Templates/SharedPointer.h"

class FMeshImportTaskRunner;

/**
 * Struct holding data used for mesh import async tasks
 */
struct FMetaHumanCharacterEditorMeshImportContext
{
	// Task runner used to run mesh import process
	TSharedPtr<FMeshImportTaskRunner> MeshImportTaskRunner;
	
	// The start time of the mesh import process
	double MeshImportStartTime = 0.0f;

	// Handle used to update the progress on mesh import
	FProgressNotificationHandle MeshImportProgressHandle;
	
	// Permanent notification item displayed while mesh importing
	TWeakPtr<SNotificationItem> MeshImportNotificationItem;

	/**
	* Marks the mesh import as finished and reset all of the state associated with it
	*/
	void MeshImportFinished();

	/**
	* Returns true if there is an active mesh import process
	*/
	bool HasActiveRequest() const;
};
