// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logic/Services/Interfaces/ISubmitToolService.h"
#include "Models/LockdownData.h"
#include "Models/SCFile.h"

class ILockdownService : public ISubmitToolService
{
public:
	virtual FSubmitToolLockdownData ArePathsInLockdown(const TArray<FSCFileRef>& InPaths, const TSet<FString>& InDynamicFiles = TSet<FString>()) = 0;
	virtual bool IsBlockingOperationRunning() const = 0;
};

Expose_TNameOf(ILockdownService);
