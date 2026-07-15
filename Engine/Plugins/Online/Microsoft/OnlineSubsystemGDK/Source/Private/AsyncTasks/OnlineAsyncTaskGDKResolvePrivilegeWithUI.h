// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OnlineAsyncTaskManagerGDK.h"
#include "OnlineExternalUIInterfaceGDK.h"
#include "OnlineSubsystemGDKTypes.h"

class FOnlineSubsystemGDK;

/**
 * Async task to write achievements
 */
class FOnlineAsyncTaskGDKResolvePrivilegeWithUI
	: public FOnlineAsyncTaskGDK
{
public:
	FOnlineAsyncTaskGDKResolvePrivilegeWithUI(
		FOnlineSubsystemGDK* InGDKInterface,
		FGDKContextHandle InGDKContext,
		FGDKUserHandle InGDKUser,
		XUserPrivilegeOptions InOptions,
		XUserPrivilege InPrivilege,
		const FOnQueryGDKResolvePrivilegeWithUICompleteDelegate& InTaskCompletionDelegate
);
	
	virtual ~FOnlineAsyncTaskGDKResolvePrivilegeWithUI() = default;

	//~ Begin FOnlineAsyncItem Interface
	virtual FString ToString() const override { return TEXT("FOnlineAsyncTaskGDKResolvePrivilegeWithUI"); }

	// Starts in Game Thread
	virtual void Initialize() override;
	// Process in Online Thread
	virtual void ProcessResults() override;

	// Move results and trigger delegates in Game Thread
	virtual void TriggerDelegates() override;

protected:
	FGDKContextHandle GDKContext;
	const FOnQueryGDKShowProfileUICompleteDelegate TaskCompletionDelegate;
	FGDKUserHandle GDKUser;
	XUserPrivilegeOptions Options;
	XUserPrivilege Privilege;
};
