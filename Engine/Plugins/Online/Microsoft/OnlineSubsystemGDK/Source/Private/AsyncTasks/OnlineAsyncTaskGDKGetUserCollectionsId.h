// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OnlineAsyncTaskManagerGDK.h"
#include "OnlineSubsystemGDKTypes.h"
#include "OnlineStoreInterfaceGDK.h"
#include "OnlineError.h"

THIRD_PARTY_INCLUDES_START
#include <XGameRuntime.h>
THIRD_PARTY_INCLUDES_END

class FOnlineSubsystemGDK;

/**
 * Async item used to marshal user collections id results from the GDK system thread to the game thread.
 */
class FOnlineAsyncTaskGetUserCollectionsId : public FOnlineAsyncTaskGDK
{
public:
	DECLARE_DELEGATE_TwoParams(FOnCompleteDelegate, const FString& /*StoreId*/, const FOnlineError& /*Result*/);

	FOnlineAsyncTaskGetUserCollectionsId(FOnlineSubsystemGDK* InGDKSubsystem, FGDKContextHandle InGDKContext, const FString& InServiceTicket, const FString& InPublisherUserId, const FOnCompleteDelegate& InDelegate);
	virtual ~FOnlineAsyncTaskGetUserCollectionsId();

	virtual void Tick() override;
	virtual void ProcessResults() override;
	
	virtual FString ToString() const override { return TEXT("FOnlineAsyncTaskGetUserCollectionsId");}
	virtual void Finalize() override;
	virtual void TriggerDelegates() override;
protected:
	FString ServiceTicket;
	FString PublisherUserId;
	FString CollectionsId;
	FOnlineError ErrorResponse;
	FOnCompleteDelegate Delegate;
	FGDKContextHandle GDKContext;
	bool bTaskStarted = false;
};
