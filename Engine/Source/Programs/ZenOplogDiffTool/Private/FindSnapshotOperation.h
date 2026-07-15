// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IZenOplogDiffOperation.h"
#include "Experimental/ZenOplogSnapshots.h"

// Uses the zen build service to lookup a snapshot descriptor for a particular build
class FFindSnapshot : public IOplogDiffOperation
{
public:
	virtual ~FFindSnapshot() = default;
	virtual ERunningState Run() override;

	// Find a snapshot based on these parameters
	FString FindProjectName;
	FString FindStream;
	FString FindPlatform;
	FString FindCommitID;
	bool bDisplayAllBuildsFound = false;

	TSharedFuture<TOptional<UE::FZenSnapshotDescriptor>> PendingResult;
	TOptional<UE::FZenSnapshotDescriptor> FoundSnapshot;
};
